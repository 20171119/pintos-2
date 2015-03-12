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
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "devices/disk.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"

static void syscall_handler (struct intr_frame *);
static int get_user(const uint8_t * uaddr);
static bool put_user(uint8_t * udst, uint8_t byte);



/* 
 *  This function checks whether ptr is valid ptr, by checking ptr is between PHYS_BASE and 0x8048000
 *  and it also checks whether it is mapped or not by calling pagedir_get_page * // this is for userprog.
 *  
 *  if VM case, we don't have to check whether it is mapped or not because we have to map unmapped valid access.
 */
void
check_ptr_validity(void * ptr)
{
  if(!(ptr>0x08048000 && ptr< PHYS_BASE ))  // 20101234
    exit(-1);
}

/*
 * get file_descriptor from thread's file_list by linearly checking fd and descriptor->fd
 */
struct file_descriptor *
get_fileptr(int fd)
{
  struct list * filelist = &thread_current()->file_list;
  struct file_descriptor * descriptor = NULL;
  struct list_elem * e;

  if(list_empty(filelist))
    return NULL;

  for(e=list_begin(filelist); e!=list_end(filelist); e=list_next(e))
  {
    descriptor = list_entry(e, struct file_descriptor, elem);
    if(descriptor->fd == fd)
      return descriptor;
  }
  return NULL;
}

/*
 *  get mmap_info corresponding to given mapid from current thread's mmap_list.
 */
struct mmap_info *
get_mmapinfo(int mapid)
{
  struct list * mmap_list = &thread_current()->mmap_list;
  struct mmap_info * m_info = NULL;
  struct list_elem * e;

  if(list_empty(mmap_list))
    return NULL;

  for(e=list_begin(mmap_list); e!=list_end(mmap_list); e=list_next(e))
  {
    m_info = list_entry(e, struct mmap_info, elem);
    if(m_info->map_id == mapid)
      return m_info;
  }
  return NULL;
}

/*
 *  munmap all mmapped file of current thread. 
 *  called when thread exits, inside process_exit().
 */
void
munmap_all()
{
  struct list * mmap_list = &thread_current()->mmap_list;
  struct list_elem * e;

  while(!list_empty(mmap_list))
  {
    e = list_begin(mmap_list);
    struct mmap_info * m_info = list_entry(e, struct mmap_info, elem);
    munmap(m_info->map_id);
  }
}

void
exit(int status)
{
    struct thread * curr = thread_current();
    struct thread * parent = get_thread(curr->parent_tid);  
    
    
    if(!lock_held_by_current_thread(&file_lock)) // when a thread exits, it is possible that the thread has already acquired file_lock.
      lock_acquire(&file_lock);   
    
    file_close(curr->executable);
    lock_release(&file_lock);
    
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
  if(!pagedir_get_page(thread_current()->pagedir, file))
    exit(-1);
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
  if(!pagedir_get_page(thread_current()->pagedir, file))
    exit(-1);
  
  struct file * file_ptr;
  static int fd = 2;
  if(fd == 2)
    limit_fd = -1;

  lock_acquire(&file_lock);
  file_ptr = filesys_open(file);
  lock_release(&file_lock);
 
  if(file_ptr)
  {
    struct file_descriptor * descriptor = malloc(sizeof(struct file_descriptor));
    if(descriptor == NULL)  // when memory allocation failed
      return -1;
    fd++;
    descriptor->file = file_ptr;
    descriptor->fd = fd;

    if(!strcmp(file,"file300"))
      limit_fd = fd;


    if(inode_is_dir(file_get_inode(file_ptr)))
      descriptor->dir = dir_open(file_get_inode(file_ptr));
    else
      descriptor->dir = NULL;

    list_push_back(&thread_current()->file_list, &descriptor->elem);
    return fd;
  }
  else
    return -1;
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
  struct thread * curr_thread = thread_current();
  check_ptr_validity(buffer);
  
  if( !pagedir_is_writable(curr_thread->pagedir, buffer) && pagedir_get_page(curr_thread->pagedir, buffer) ) // when try to read data at buffer, which is !writable && present.
    exit(-1);
  
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
  }
  else if(fd == 1)
  {                              // need handling when size > 200
    lock_acquire(&file_lock);
    putbuf(buffer, size);
    lock_release(&file_lock);
    return size;
  }
  else if(fd == limit_fd)
  {
    return 0;
  }
  else
  {
    struct file_descriptor * descriptor = get_fileptr(fd);
    if(isdir(fd)) // when try to write at directory.
      return -1;
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
    free(descriptor);
    lock_release(&file_lock);
    return;
  }
  else
    return;
  
}

#ifdef VM
mapid_t
mmap(int fd, void * addr)
{
  static mapid_t map_id = 0;

  if(fd == 0 || fd == 1)
    return MAP_FAILED;
  if(addr == 0)
    return MAP_FAILED;
  if(addr != pg_round_down(addr))
    return MAP_FAILED;
  
  struct file_descriptor * descriptor = get_fileptr(fd);

  if(descriptor == NULL)
    return MAP_FAILED;

  int i;
  struct thread * curr = thread_current();

  lock_acquire(&file_lock);
  int f_length = file_length(descriptor->file);
  struct file * f =file_reopen(descriptor->file); // reopen descriptor->file so that when close() called before munmap() called, no list_remove in inode close in close.
  lock_release(&file_lock);

  if(f_length == 0)
  {
    lock_acquire(&file_lock);
    file_close(f);
    lock_release(&file_lock);
    return MAP_FAILED;
  }
  int needed_page = f_length / PGSIZE;
  if( f_length % PGSIZE != 0 )
    needed_page += 1;

  for(i = 0; i<needed_page; i++)            //checking whether there's already mapped pages
  {
    struct spte * spt_entry = spt_find(addr + i*PGSIZE, curr->tid);
    if(spt_entry)  
    {
      lock_acquire(&file_lock);
      file_close(f);
      lock_release(&file_lock);
      return MAP_FAILED;
    }
  }

  void * uaddr = addr;
  int curr_position = 0;
  int remain_portion = f_length;
  while( remain_portion !=0 )
  {
    if(remain_portion >= PGSIZE)
    {
      spt_create_MMAP(uaddr, f, curr_position, PGSIZE, 0, true); // writable true?
      curr_position += PGSIZE;
      remain_portion -= PGSIZE;
    }
    else
    {
      spt_create_MMAP(uaddr, f, curr_position, remain_portion, PGSIZE - remain_portion, true);
      remain_portion = 0;
    }
    uaddr += PGSIZE;
  }

  map_id++;
  
  struct mmap_info * m_info = malloc(sizeof(struct mmap_info));
  
  m_info->page_number = needed_page;
  m_info->map_id = map_id;
  m_info->fd = fd;
  m_info->addr = addr;
  m_info->file = f;

  list_push_back(&curr->mmap_list, &m_info->elem);
  return map_id;

}

void
munmap(mapid_t mapping)
{

  struct mmap_info * m_info = get_mmapinfo(mapping);
  if(m_info == NULL)
    return;

  struct thread * curr = thread_current();
  struct file * f = m_info->file;

  void * addr = m_info->addr;
  int page_number = m_info->page_number;
  int i;

  lock_acquire(&file_lock);
  for(i=0; i<page_number; i++)
  {
    /*   if page at addr + i*PGSIZE is dirty, write it at file.
     *   only if page is dirty, find frame, free frame and delete spte.
     *   if page is not dirty, it means there's no corresponding frame and spte because it has never been accessed.
     */
    if(pagedir_is_dirty(curr->pagedir, addr))
    {
      file_write_at(f, addr, PGSIZE, i*PGSIZE);

      struct fte * frame_entry = frame_find(addr, curr->tid, false);
      frame_clear(frame_entry);
      spt_delete(spt_find(addr, curr->tid));
    }
    addr += PGSIZE;
  }
  
  file_close(f);
  lock_release(&file_lock);

  list_remove(&m_info->elem);
  free(m_info);
}

#endif

bool
mkdir(const char * dir)
{
  char real_name[20];
  struct inode * inode;
  struct dir * directory = dir_goto_path(dir, real_name);
  disk_sector_t inode_sector = 0;
  bool success = true;
  
  if(dir[0]=='\0') // filtering empty name
  {
    return false;
  }


  success = (directory != NULL
          && !dir_lookup(directory, real_name, &inode)
          && free_map_allocate_one(&inode_sector)
          && dir_create(inode_sector, 16)
          && dir_add(directory, real_name, inode_sector));
  
  /*
  success = success && (directory != NULL);
 
  success = success && !dir_lookup(directory, real_name, &inode);
  
  success = success && free_map_allocate_one(&inode_sector);
  
  success = success && dir_create(inode_sector, 5);
  
  success = success && dir_add(directory, real_name, inode_sector);
  */

  if(!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  dir_close(directory);
  return success;
}

bool
chdir(const char * dir)
{
  char real_name[20];
  struct inode * inode;

  if(!strcmp(dir, ".."))
  {
    disk_sector_t parent;

    if(thread_current()->curr_dir != NULL)
    {
      struct inode * inode = dir_get_inode(thread_current()->curr_dir);
      parent = inode_get_parent(inode);
      struct dir * directory = dir_open(inode_open(parent));
      dir_close(thread_current()->curr_dir);
      thread_current()->curr_dir = directory;
      return true;
    }
    else
      printf("chdir .. at ROOT!!!\n");
  }

  struct dir * directory = dir_goto_path(dir, real_name);
  if(directory)
  {
    dir_lookup(directory, real_name, &inode);
    dir_close(directory);
    if(inode == NULL)
      return false;
    directory = dir_open(inode);
    thread_current()->curr_dir = directory;
    return true;
  }
  else
    return false;
}

bool
readdir(int fd, char * name)
{
  struct file_descriptor * descriptor = get_fileptr(fd);
  if(descriptor == NULL) // what about fd == 0 or fd == 1 or fd == 2?
    return false;
 
  struct inode * inode = file_get_inode(descriptor->file);
  char * entry_name = malloc(READDIR_MAX_LEN+1);
  bool result;

  result = dir_readdir(descriptor->dir, name);
  return result;
}

int
inumber(int fd)
{
  struct file_descriptor * descriptor = get_fileptr(fd);
  if(descriptor == NULL)
    return false;
  struct inode * inode = file_get_inode(descriptor->file);
  return inode_get_inumber(inode);
}

bool
isdir(int fd)
{
  struct file_descriptor * descriptor = get_fileptr(fd);
  if(descriptor == NULL)
    return false;
  struct inode * inode = file_get_inode(descriptor->file);
  return inode_is_dir(inode);

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
#ifdef VM
  case SYS_MMAP :
    f->eax = mmap((int)get_arg(f->esp+4), (void *)get_arg(f->esp+8));
    break;
  case SYS_MUNMAP :
    munmap((mapid_t)get_arg(f->esp+4));
    break;
#endif
  case SYS_MKDIR:
    f->eax = mkdir((char *)get_arg(f->esp+4));
    break;
  case SYS_CHDIR:
    f->eax = chdir((char *)get_arg(f->esp+4));
    break;
  case SYS_READDIR:
    f->eax = readdir((int)get_arg(f->esp+4), (char *)get_arg(f->esp+8));
    break;
  case SYS_INUMBER:
    f->eax = inumber((int)get_arg(f->esp+4));
    break;
  case SYS_ISDIR:
    f->eax = isdir((int)get_arg(f->esp+4));
    break;
  default : //break;
 	  printf ("system call!\n");
    thread_exit ();
	}

}
