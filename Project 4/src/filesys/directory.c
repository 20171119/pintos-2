#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"


/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    disk_sector_t inode_sector;         /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
//    disk_sector_t parent;
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) 
{
  
  bool result;
  struct inode_disk temp;
  disk_sector_t parent;

  if(thread_current()->curr_dir != NULL)
  {
    struct inode * inode = dir_get_inode(thread_current()->curr_dir);
    parent = inode_get_inumber(inode);
  }
  else
    parent = ROOT_DIR_SECTOR;

  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true, parent); // true means creating directory
//  buffer_read(sector, &temp, 0, DISK_SECTOR_SIZE);
//  temp.parent = parent;
//  buffer_write(sector, &temp, 0, DISK_SECTOR_SIZE);
//  return result;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}


/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
  {
    //printf("in lookup e.name is %s\n",  e.name);
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        //printf("in lookup return true\n");
        return true;
      }
  }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /*
  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;
  */
  
  if( lookup(dir, name, &e, NULL) )
  {
    if(e.in_use)
    {
      //printf("in dir_lookup, exists\n");
      *inode = inode_open(e.inode_sector);
      //printf("in dir_lookup, after inode_open(e.inode_sector)\n");
    }
    else
    {
      //printf("not used in dir_lookup\n");
      *inode = NULL;
    }
  }
  else
    *inode = NULL;
  //printf("dir_lookup end\n");
//  if(*inode != NULL)
//    printf("success\n");
//  else
//    printf("dir_lookup fail\n");
  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) 
{
  //printf("dir_entry size is %d\n", sizeof (struct dir_entry));
  struct dir_entry e;
  off_t ofs;
  bool success = false;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */

  int i=0;
  //printf("size of e is %d\n", sizeof e);
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
  {
    i++;
    //printf("aa ");
    //printf("dir_entry name is %s\n", e.name);
    if (!e.in_use)
    {
      //printf("not in use, i is %d\n", i);
      break;
    }
  }
  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  /*
  if(thread_current()->curr_dir != NULL) // set dir_entry's parent.
  {
    struct inode * inode = dir_get_inode(thread_current()->curr_dir);
    e.parent = inode_get_inumber(inode);
  }
  else
  {
    e.parent = ROOT_DIR_SECTOR;
  }
  */
  //printf("in dir_add , name is %s\n", name);
  //printf("ofs is %d\n", ofs);
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
  
 done:
  //printf("dir_add end\n");
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  
  //printf("dir_remove, name is %s\n", name);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;
  //printf("middle in dir_remove\n");
  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;
  //printf("after middle\n");
  /* Remove inode. */
  inode_remove (inode);
  success = true;
  //printf("dir_remove success\n");
 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;
  //printf("in dir_readdir pos is %d\n", dir->pos);
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      //printf("pos is %d\n", dir->pos);
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

struct dir *
dir_goto_path(char * dir, char * real_name)
{
  //printf("dir_goto_path start. %s\n",dir);
  char * path = malloc(20);
  char * original_path = path;
  strlcpy(path, dir, 20);
  char * ptr;
  char * token;
  char * prev_token;
  struct thread * curr_thread = thread_current();
  struct dir * current;
  struct inode * inode;
  int token_cnt = 1;  
  
  if(path[0] == '/')
  {
    //printf("absolute\n");
    current = dir_open_root();
    path += 1;
  }
  else
  {
    if(curr_thread->curr_dir == NULL)
    {
//      printf("curr_dir is NULL\n");
      current = dir_open_root();
      //curr_thread->curr_dir = dir_open_root();
    }
    else
    {
//      printf("relative, curr_thread->curr_dir not null\n");
      current = dir_reopen(curr_thread->curr_dir);
    }
  }
  token = strtok_r(path, "/", &ptr);
  if(token == NULL)
  {
    real_name[0] = '\0';
    free(original_path);
    return current;
  }
//  printf("first token is %s\n", token);

  while(token)
  {
    //printf("in while loop token is %s\n", token);
    prev_token = token;
    token = strtok_r(NULL, "/", &ptr);
    token_cnt++;
    //printf("prev_token is %s\n", prev_token);
    if(token == NULL) // when this token is last token.
    {
      //printf("break\n");
      break;
    }
    if( dir_lookup(current, prev_token, &inode) ) // when there's dir_entry named prev_token
    {
      dir_close(current);
      current = dir_open(inode);
      //prev_token = token;
      //token = strtok_r(NULL, "/", &ptr);
    }
    else
    {
      //printf("no directory named %s\n", token);
      dir_close(current);
      free(original_path);
      return NULL;
    }

  }
  
  // now prev_token is the last name.
  strlcpy(real_name, prev_token, 20);
  //printf("end of dir_goto, real_name is %s\n", real_name);
  free(original_path);
  return current;

}

bool
dir_is_empty(struct dir * dir)
{
//  printf("dir is empty\n");
  struct inode * inode = dir->inode;
  struct dir_entry e;
  off_t ofs;
  ofs = 0;  
  if(inode == NULL)
    printf("inode is NULL\n");

  for(ofs = 0; inode_read_at(inode, &e, sizeof e, ofs) == sizeof e; 
      ofs += sizeof e)
  {
    if(e.in_use)
    {
//      printf("dir_is_empty return\n");
      return false;
    }
//    else
//      printf("hihi\n");
  }
//  printf("return true\n");
  return true;

}


