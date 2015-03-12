#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), char **esp);
struct semaphore exec_sema;

/* new function */
struct child_process * get_child(int pid, struct list * childlist); // get child process from current thread's child_list by checking pid
/* end */

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char * fn_copy;
  char * fn_copy2;
  tid_t tid;

  char * token;
  char * ptr;
  struct file * executable;

  sema_init(&exec_sema, 0);
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = malloc(128);
  fn_copy2 = malloc(128);
  
  if (fn_copy == NULL)
    return TID_ERROR;
  
  strlcpy(fn_copy, file_name, 128);
  strlcpy(fn_copy2, file_name, 128);

  token = strtok_r(fn_copy2, " ", &ptr );

  tid = thread_create(token, PRI_DEFAULT, start_process, fn_copy);

  sema_down(&exec_sema);

  struct thread * child_thread = get_thread(tid);


  if(child_thread == NULL)
    return -1;
  
  thread_unblock(child_thread);


  if(!child_thread->is_loaded) // when load failed
    return -1;
  
  if (tid == TID_ERROR)
  {
    free(fn_copy);
  }
  else // success in thread_create
  {

    struct thread * current = thread_current();
    struct child_process * child = palloc_get_page(0);
    child->pid = tid;
    child->is_exited = false;
    child->is_p_waiting = false;
    child->exit_status = -2 ; // need??
    
    list_push_back(&current->child_list, &child->elem);
  }
  free(fn_copy2);
  return tid;
}

/* A thread function that loads a user process and makes it start
   running. */
static void
start_process (void *f_name)
{
  char *file_name = f_name;
  struct intr_frame if_;
  bool success;

//  printf("start of start_process\n");
  struct thread * curr = thread_current();

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  

  success = load (file_name, &if_.eip, &if_.esp);
  curr->is_loaded = success;
  sema_up(&exec_sema);

  enum intr_level old_level = intr_disable();
  thread_block();
  intr_set_level(old_level);
  
  
  /* If load failed, quit. */
  free (file_name);
  if (!success){
    thread_exit ();
  }
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
 
  int exitstatus; 

//  printf("in process wait\n");
//  printf("thread_name is %s\n", thread_current()->name);
  struct child_process * child = get_child(child_tid, &thread_current()->child_list);
  
  if(child == NULL)
    return -1;
  struct thread * child_ptr = get_thread(child->pid);

  if(child == NULL)
  {
    exit(-1);
    return -1;
  }
  else
  {
    if(child->is_exited)
    {
      list_remove(&child->elem);
      exitstatus = child->exit_status;
      palloc_free_page(child);
      return exitstatus;

    }
    else // when need to wait
    {
      if(child->is_p_waiting) // already called
        return -1;
      child->is_p_waiting = true;

      enum intr_level old_level = intr_disable();
      thread_block();
      intr_set_level(old_level);
      list_remove(&child->elem);
      exitstatus = child->exit_status;
      palloc_free_page(child);
      return exitstatus;
    }
  }
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *curr = thread_current ();
  uint32_t *pd;
  
//  printf("in process_exit\n");
  munmap_all();

  /*
   * close all file_descriptors in current thread's file_list
   */
  struct list_elem * e;
  while(!list_empty(&curr->file_list))
  {
    e = list_begin(&curr->file_list);
    struct file_descriptor * descriptor = list_entry(e, struct file_descriptor, elem);
    close(descriptor->fd);
  }

  while(!list_empty(&curr->child_list))
  {
    e = list_begin(&curr->child_list);
    struct child_process * child = list_entry(e, struct child_process, elem);
    free(child);
  }
  

  spt_exit();
  frame_free_tid(curr->tid);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = curr->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      curr->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), char **esp) 
{

  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();
  

  /* new */
  char * fn_copy;
  fn_copy = malloc(128);
  
  if(fn_copy == NULL)
    return TID_ERROR;

  strlcpy(fn_copy, file_name, 128);

  char * real_name;
  char * sptr;

  real_name = strtok_r(fn_copy, " ", &sptr);

  
  file = filesys_open(real_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;


  /* argument passing part */
  int argc = 0;
  int arg_len;
  int align_byte;
  char * ptr;
  char ** argv = malloc(30*4);
  char ** arg = malloc(30*4);
  char * token;
  char * argv_ptr;
 
  token = strtok_r(file_name, " ", &ptr);
  while(token)
  {
    arg[argc] = token;
    argc++;
    token = strtok_r(NULL, " ", &ptr);
  }

  for(i=argc-1; i>=0; i--)   // push arg from back to front
  {
    arg_len = strlen(arg[i]) + 1;
    *esp = *esp - arg_len;
    memcpy(*esp, arg[i], arg_len);
    argv[i] = *esp;
  }
  argv[argc] = 0; 
  
  align_byte = ((int) *esp) & 3;
  
  if(align_byte)  //push align_bytes
  {
    *esp = *esp-align_byte;
  }

 
  *esp = *esp - 4;  //push argv[argc], which is NULL
  memset(*esp, 0, 4);

  for(i=argc-1; i>=0; i--) //push argv[argc-1] to argv[0] into stack
  {
    *esp = *esp - 4;
    memcpy(*esp, &argv[i], 4);
  }
  *esp = *esp - 4;              // push argv
  argv_ptr = *esp + 4;
  memcpy(*esp,&argv_ptr, 4);

  *esp = *esp - 4;              //push argc
  memcpy(*esp, &argc, 4);
 
  *esp = *esp - 4;             //push NULL return address
  memset(*esp, 0 , 4);


  /*  end   */
  
  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  free(argv);
  free(arg);
  success = true;
  
 done:
  /* We arrive here whether the load is successful or not. */
 
//  printf("end of load\n");
 
  if(success)
  {
    lock_acquire(&file_lock);
    file_deny_write(file);
    lock_release(&file_lock);
    thread_current()->executable = file;
  }
  else
  {
    lock_acquire(&file_lock);
    file_close(file);
    lock_release(&file_lock);
  }

  free(fn_copy);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

#ifdef VM

  file_seek(file, ofs);
  while(read_bytes>0 || zero_bytes >0)
  {

    size_t page_read_bytes = read_bytes < PGSIZE? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;
    
    spt_create_FILE(upage, file, ofs, page_read_bytes,  page_zero_bytes, writable);

    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
    ofs += PGSIZE;
  }
  return true;

#else

    
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Do calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;

#endif
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;
 
#ifdef VM
  kpage = frame_alloc();

  void * target_uaddr = ((uint8_t *)PHYS_BASE)-PGSIZE;

  if(kpage == NULL)
  {
    struct fte * evicted_frame = frame_select_evict();
    swap_out(evicted_frame->uaddr, evicted_frame->tid);
    kpage = frame_alloc();
  }
  
  
  success = install_page(target_uaddr , kpage, true) && spt_create_PRESENT(target_uaddr, kpage, true);
 // success = success && spt_create_PRESENT(target_uaddr, kpage, true);
    
  struct fte * frame_entry = frame_find(kpage, thread_current()->tid, true);
   
  frame_set_uaddr(frame_entry, target_uaddr);
  frame_set_complete(frame_entry);
  
  if(success)
  {
    *esp = PHYS_BASE;
  }
  else
  {
    struct fte * frame_entry = frame_find(kpage, thread_current()->tid, true); // we can't use frame_find(target_uaddr, tid, false) because we don't set the frame_entry->uaddr.
    frame_free(frame_entry);
  }
  
  return success; 

#else
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
#endif
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* new added */

struct child_process * 
get_child(int pid, struct list * childlist)
{
  if(list_empty(childlist))
    return NULL;

  struct child_process * child;
  struct list_elem * e;
  for( e=list_begin(childlist); e!=list_end(childlist); e=list_next(e))
  {
    child = list_entry(e, struct child_process, elem);
    if(child->pid == pid)
      return child;
  }
  return NULL;
}
