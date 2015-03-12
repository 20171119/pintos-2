#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/cache.h"
#include "userprog/syscall.h"
#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  filesys_disk = disk_get (0, 1);
  if (filesys_disk == NULL)
    PANIC ("hd0:1 (hdb) not present, file system initialization failed");

  inode_init ();
  buffer_init();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  buffer_flush_all();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  disk_sector_t inode_sector = 0;
  disk_sector_t parent;
  /* */
  if(thread_current()->curr_dir != NULL)
  {
    struct inode * inode = dir_get_inode(thread_current()->curr_dir);
    parent = inode_get_inumber(inode);
  }
  else
    parent = -1;

  char * real_name = malloc(30);
  struct dir * dir = dir_goto_path(name, real_name);
    
  struct inode * inode = dir_get_inode(dir);
  if(inode_is_removed(inode))
  {
    free(real_name);
    dir_close(dir);
    return false;
  }
  
  bool success = (dir != NULL
                  && free_map_allocate_one (&inode_sector)
                  && inode_create (inode_sector, initial_size, false, parent) // false means creating file. not directory
                  && dir_add (dir, real_name, inode_sector));
  
  
  if (!success && inode_sector != 0)
  {
    free_map_release (inode_sector, 1);
  }
  dir_close (dir);
  free(real_name);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  char * real_name = malloc(30);
  
  if(!strcmp(name, "."))
  {
    free(real_name);
    if(thread_current()->curr_dir != NULL)
    {
      if(inode_is_removed(dir_get_inode(thread_current()->curr_dir)))
        return NULL;
      
      struct dir * result = dir_reopen(thread_current()->curr_dir);
      return file_open(dir_get_inode(result));
    }
    else
    {
      return file_open(inode_open(ROOT_DIR_SECTOR));
    }
  }
  
  struct dir * dir = dir_goto_path(name, real_name);
  struct inode *inode = NULL;
  if(real_name[0] == '\0') // when name is "/"
  {
    dir_close(dir);
    free(real_name);
    struct inode * inode = inode_open(ROOT_DIR_SECTOR);
    if(inode==NULL)
    {
      return false;
    }
    struct file * result = file_open(inode);
    if(result ==NULL)
    {
      return false;
    }
    return result;
  }
  
  if (dir != NULL)
    dir_lookup (dir, real_name, &inode);
  if(inode == NULL)
  {
    return NULL;
  }
  dir_close (dir);
  free(real_name);
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  char * real_name = malloc(30);
  struct dir * dir = dir_goto_path(name, real_name);
  if(dir == NULL)
  {
    //printf("dir is null\n");
    free(real_name);
    return false;
  }
   
  if(inode_get_inumber(dir_get_inode(dir)) == ROOT_DIR_SECTOR && real_name[0] == '\0') // when try to remove root directory
  {
    dir_close(dir);
    free(real_name);
    return false;
  }
  
  struct inode * inode;
  if(!dir_lookup(dir, real_name, &inode)) // there's no entry for real_name
  {
    dir_close(dir);
    free(real_name);
    return false;
  }

  if(inode_is_dir(inode)) // find whether real_name is file or directory. if it's directory,
  {
    struct dir * target_dir = dir_open(inode);
    if(!dir_is_empty(target_dir))
    {
      dir_close(dir);
      free(real_name);
      return false;
    }
  }
  bool success = dir != NULL && dir_remove (dir, real_name);
  dir_close (dir); 
  free(real_name);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
