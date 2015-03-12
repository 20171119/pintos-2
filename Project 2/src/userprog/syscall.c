#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/init.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static int get_user(const uint8_t * uaddr);
static bool put_user(uint8_t * udst, uint8_t byte);

void check_ptr_validity(void * ptr);
struct file_descriptor * get_fileptr(int fd);
void halt(void);
void exit(int status);
int exec(const char * cmd_line);
bool create(const char * file, unsigned initial_size);
bool remove(const char * file);
int open(const char * file);
int filesize(int fd);
int read(int fd, void * buffer, unsigned size);
int write(int fd, const void * buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
bool check_ptr_validity_open(void * ptr);

/* 
 *  This function checks whether ptr is valid ptr, by checking ptr is between PHYS_BASE and 0x8048000
 *  and it also checks whether it is mapped or not by calling pagedir_get_page *
 */
void
check_ptr_validity(void * ptr)
{
  if(!(ptr>0x08048000 && ptr< PHYS_BASE ))
  {
    exit(-1);
  }
  void * pointer = pagedir_get_page(thread_current()->pagedir, ptr); //check if mapped
  if(!pointer)
  {
    exit(-1);
  }
  
}

/*
 * get file_descriptor from thread's file_list by linearly checking fd and descriptor->fd
 */
struct file_descriptor *
get_fileptr(int fd)
{
  struct list * filelist = &thread_current()->file_list;
  struct file_descriptor * descriptor = NULL;
  struct file_descriptor * temp;
  struct list_elem * e;

  if(list_empty(filelist))
    return NULL;

  for(e=list_begin(filelist); e!=list_end(filelist); e=list_next(e))
  {
    temp = list_entry(e, struct file_descriptor, elem);
    if(temp->fd == fd)
    {
      descriptor = temp;
      break;
    }
  }
  return descriptor;
}

void
exit(int status)
{
    struct thread * curr = thread_current();
    struct thread * parent = get_thread(curr->parent_tid);  

    if(curr->executable)
    {
      lock_acquire(&file_lock);
      file_close(curr->executable);
      lock_release(&file_lock);
    }

    struct list_elem * e;
    
    /*
     *  We do nothing about orphaned child, which is the case of parent == NULL
     */
    if(parent)
    {
      struct child_process * self = get_child(curr->tid, &parent->child_list);
      if(self != NULL)
      {
        self->is_exited = true;
        self->exit_status = status;
        if(self->is_p_waiting)
        {
          enum intr_level old_level = intr_disable();
          thread_unblock(parent); //intr on off?
          intr_set_level(old_level);
        }
      }
    }
		printf("%s: exit(%d)\n", curr->name, status);
    thread_exit();
}

int
exec(const char * cmd_line)
{
  check_ptr_validity(cmd_line);
  return process_execute(cmd_line);
}

bool
create(const char * file, unsigned initial_size)
{
  check_ptr_validity(file);
  
  int result;
  lock_acquire(&file_lock);
  result = filesys_create(file, initial_size);
  lock_release(&file_lock);
  return result;
  
}

bool 
remove(const char * file)
{
  check_ptr_validity(file);
  
  int result;
  lock_acquire(&file_lock);
  result = filesys_remove(file);
  lock_release(&file_lock);
  return result;
  
}

int
open(const char * file)
{
  check_ptr_validity(file);
  
  struct file * file_ptr;
  static int fd = 2;
  
  lock_acquire(&file_lock);
  file_ptr = filesys_open(file);
  lock_release(&file_lock);
  if(file_ptr)
  {
    struct file_descriptor * descriptor = palloc_get_page(0);
    if(descriptor == NULL)  // when memory allocation failed
      return -1;
    fd++;
    descriptor->file = file_ptr;
    descriptor->fd = fd;
    list_push_back(&thread_current()->file_list, &descriptor->elem);
    return fd;
  }
  else
  {
    return -1;
  }
}

int
filesize(int fd)
{
  struct file_descriptor * descriptor = get_fileptr(fd);
  if(descriptor)
  {
    int result;
    lock_acquire(&file_lock);
    result = file_length(descriptor->file);
    lock_release(&file_lock);

    return result;
  }
  return -1; 
}

int
read(int fd, void * buffer, unsigned size)
{
  check_ptr_validity(buffer);

  struct file_descriptor * descriptor = get_fileptr(fd);

  if(fd == 0)
  {
    int i=0;
    lock_acquire(&file_lock);
    while(size)
    {
      *((char *)buffer+i) = input_getc();
      size--;
      i++;
    }
    lock_release(&file_lock);
    return size;
  }
  if(descriptor)
  {
    int result;
    lock_acquire(&file_lock);
    if(!descriptor->file)
      return -1;
    result = file_read(descriptor->file, buffer, size);
    lock_release(&file_lock);
    return result;
  }
else
    return -1;
}

int
write(int fd, const void * buffer, unsigned size)
{
  check_ptr_validity(buffer);

  if(fd == 0 || fd==2)
  {
    exit(-1);
    return -1;
  }
  else if(fd == 1)
  {                              // need handling when size > 200
    lock_acquire(&file_lock);
    putbuf(buffer, size);
    lock_release(&file_lock);
    return size;
  }
  else
  {
    struct file_descriptor * descriptor = get_fileptr(fd);
    
    if(descriptor)
    {
      int result;
      lock_acquire(&file_lock);
      result = file_write(descriptor->file, buffer, size);
      lock_release(&file_lock);
      return result;
    }
  }
}

void
seek(int fd, unsigned position)
{
  struct file_descriptor * descriptor = get_fileptr(fd);

  if(descriptor)
  {
    lock_acquire(&file_lock);
    file_seek(descriptor->file, position);
    lock_release(&file_lock);
  }
  else
    return;
}

unsigned
tell(int fd)
{
  struct file_descriptor * descriptor = get_fileptr(fd);

  if(descriptor)
  {
    unsigned result;
    lock_acquire(&file_lock);
    result = file_tell(descriptor->file);
    lock_release(&file_lock);
    return result;
  }
  else
    return 0; // ??
}

void
close(int fd)
{
  struct file_descriptor * descriptor = get_fileptr(fd);

  if(descriptor)
  {
    lock_acquire(&file_lock);
    file_close(descriptor->file);
    list_remove(&descriptor->elem);
    palloc_free_page(descriptor);
    lock_release(&file_lock);
    return;
  }
  else
    return;
  
}



/* Reads a byte at user virtual address UADDR.
 * UADDR must be below PHYS_BASE.
 * Returns the byte value if successful, -1 if a segfault
 * occured.
 */
static int
get_user (const uint8_t * uaddr)
{
	int result;
	asm ("movl $1f, %0; movzbl %1, %0; 1:"
				: "=&a" (result) : "m" (*uaddr));
	return result;
}

/* Writes BYTE to user address UDST.
 * UDST must be below PHYS_BASE.
 * Returns true if successful, false if a segfault occured.
 */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
	int error_code;
	asm ("movl $1f, %0; movb %b2, %1; 1:"
				: "=&a" (error_code), "=m" (*udst) : "q" (byte));
	return error_code != -1;
}


int
get_arg(char * ptr)
{
  unsigned temp = 0;
  unsigned retval = 0;
  check_ptr_validity(ptr);

  temp = get_user(ptr);
  if(temp == -1) 
    exit(-1);
  else
    retval = retval | (temp);

  temp = get_user(ptr+1);
  if(temp == -1)
    exit(-1);
  else
    retval = retval | (temp << 8);
  
  temp = get_user(ptr+2);
  if(temp == -1)
    exit(-1);
  else
    retval = retval | (temp << 16);

  temp = get_user(ptr+3);
  if(temp == -1)
    exit(-1);
  else
    retval = retval | (temp << 24);

  return retval;

}



void
syscall_init (void) 
{
  lock_init(&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f ) 
{
	check_ptr_validity(f->esp);

  switch((int)get_arg(f->esp))
	{
	case SYS_HALT :
    power_off();
		break;
	case SYS_EXIT :
    exit((int)get_arg(f->esp+4));
		break;
	case SYS_EXEC :
    f->eax = (uint32_t) exec((char *) get_arg(f->esp+4));
    break;
  case SYS_WAIT :
    f->eax = (uint32_t) process_wait(get_arg(f->esp+4));
    break;
	case SYS_CREATE : 
    f->eax = create((char *)get_arg(f->esp+4), (unsigned)get_arg(f->esp+8));
    break;
	case SYS_REMOVE : 
    f->eax = remove((char *)get_arg(f->esp+4));
    break;
	case SYS_OPEN : 
    f->eax = open((char *)get_arg(f->esp+4));
    break;
	case SYS_FILESIZE : 
    f->eax = filesize((int)get_arg(f->esp+4));
    break;
	case SYS_READ : 
    f->eax = read((int)get_arg(f->esp+4), (void *)get_arg(f->esp+8), get_arg(f->esp+12));
    break;
	case SYS_WRITE :
    f->eax = write((int)get_arg(f->esp+4), (void *)get_arg(f->esp+8), get_arg(f->esp+12));
    break;
	case SYS_SEEK :
    seek((int)get_arg(f->esp+4), get_arg(f->esp+8));
    break;
	case SYS_TELL : 
    f->eax = tell((int)get_arg(f->esp+4));
    break;
	case SYS_CLOSE : 
    close((int)get_arg(f->esp+4));
    break;
	default : //break;
 	  printf ("system call!\n");
    thread_exit ();
	}

}
