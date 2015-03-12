#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include <round.h>

static struct file *free_map_file;   /* Free map file. */
static struct bitmap *free_map;      /* Free map, one bit per disk sector. */

/* Initializes the free map. */
void
free_map_init (void) 
{
  lock_init(&free_map_lock);
  free_map = bitmap_create (disk_size (filesys_disk));
  if (free_map == NULL)
    PANIC ("bitmap creation failed--disk is too large");
  bitmap_mark (free_map, FREE_MAP_SECTOR);
  bitmap_mark (free_map, ROOT_DIR_SECTOR);
}

bool
free_map_allocate_one(disk_sector_t * sectorp)
{
  lock_acquire(&free_map_lock);
  disk_sector_t sector = bitmap_scan_and_flip(free_map, 0, 1, false);
//  printf("in allocate_one, sector is %d\n", sector);
  if(sector != BITMAP_ERROR && bitmap_write(free_map, free_map_file))
    *sectorp = sector;
  lock_release(&free_map_lock);
  return sector != BITMAP_ERROR;
}

/* Allocates CNT consecutive sectors from the free map and stores
   the first into *SECTORP.
   Returns true if successful, false if all sectors were
   available. */
bool
free_map_allocate(size_t cnt, struct inode_disk * disk_inode)
{
 // printf("free_map_allocate start, cnt is %d\n",cnt);
 
  lock_acquire(&free_map_lock);
  size_t free_slot = bitmap_count(free_map, 0, bitmap_size(free_map)-1, false);
  if(free_slot < cnt)
  {
    lock_release(&free_map_lock);
    return false;
  }
  int i,j;
  disk_sector_t target;
  disk_sector_t single_indirect;
  disk_sector_t double_indirect;
  disk_sector_t * single;


  if(cnt <= DIRECT_NUM) // DIRECT case
  {
    for(i=0; i<DIRECT_NUM; i++)
    {
      if(i<=cnt-1) // if cnt ==0, cnt-1 can be interpreted as unsigned?
      {
        target = bitmap_scan_and_flip(free_map, 0, 1, false);
        disk_inode->direct[i] = target;
      }
      else
        disk_inode->direct[i] = -1;
    }
    if(free_map_file == NULL) // when this function is called by free_map_create, free_map_file is NULL.
    {
      lock_release(&free_map_lock);
      return target != BITMAP_ERROR;
    }
    bitmap_write(free_map, free_map_file);
    lock_release(&free_map_lock);
    return true;
  }
  else if(cnt > DIRECT_NUM && cnt <= (DIRECT_NUM + DISK_SECTOR_SIZE/4)) // single_indirect case
  {
    for(i=0; i<DIRECT_NUM; i++)
    {
      target = bitmap_scan_and_flip(free_map, 0, 1, false);
      disk_inode->direct[i] = target;
    }
    bitmap_write(free_map, free_map_file);

    //single indirect
    single_indirect = bitmap_scan_and_flip(free_map, 0, 1, false);
    disk_inode->single_indirect = single_indirect;
    single = malloc(DISK_SECTOR_SIZE);

    for(i=0; i< DISK_SECTOR_SIZE/4; i++)
    {
      if(i<=cnt - DIRECT_NUM -1)
      {
        target = bitmap_scan_and_flip(free_map, 0, 1, false);
        single[i] = target;
      }
      else
        single[i] = -1;
    }
    bitmap_write(free_map, free_map_file);
    buffer_write(single_indirect, single, 0, DISK_SECTOR_SIZE);
    free(single);
    lock_release(&free_map_lock);
    return true;
  }
  else // double_indirect case
  {
    // doing direct allocation
    for(i=0; i<DIRECT_NUM; i++)
    {
      disk_inode->direct[i] = bitmap_scan_and_flip(free_map, 0, 1, false);
    }
    bitmap_write(free_map, free_map_file);
    
    // doing single_indirect allocation.
    single_indirect = bitmap_scan_and_flip(free_map, 0, 1, false);
    disk_inode->single_indirect = single_indirect;
    single = malloc(DISK_SECTOR_SIZE);

    for(i=0; i<DISK_SECTOR_SIZE/4; i++)
    {
      single[i] = bitmap_scan_and_flip(free_map, 0, 1, false);
    }
    bitmap_write(free_map, free_map_file);
    buffer_write(single_indirect, single, 0, DISK_SECTOR_SIZE);
    free(single);

    //doing double_indirect allocation
    double_indirect = bitmap_scan_and_flip(free_map, 0, 1, false);
    disk_inode->double_indirect = double_indirect;
    disk_sector_t * double_table = malloc(DISK_SECTOR_SIZE);
    disk_sector_t * single = malloc(DISK_SECTOR_SIZE);
    off_t double_pos = cnt - DOUBLE_INDIRECT_START;
    off_t single_pos = double_pos % (512 * 128);

    int double_cnt = cnt - DOUBLE_INDIRECT_START;

    disk_sector_t entry_num_in_double = DIV_ROUND_UP(double_cnt, (512 * 128));
    int temp = double_cnt % (512 * 128);
    int entry_num_in_single = DIV_ROUND_UP(temp, DISK_SECTOR_SIZE);


    for(i=0; i<entry_num_in_double; i++)
    {
      double_table[i] = bitmap_scan_and_flip(free_map, 0, 1, false);
      
      if(i!=entry_num_in_double-1) // do fully allocate single_indirect
      {
        for(j=0; j<DISK_SECTOR_SIZE/4; j++)
        {
          single[j] = bitmap_scan_and_flip(free_map, 0, 1, false);
        }
        buffer_write(double_table[i], single, 0, DISK_SECTOR_SIZE);
      }
      else
      {
        for(j=0; j<DISK_SECTOR_SIZE/4; j++)
        {
          if(j<entry_num_in_single)
          {
            single[j] = bitmap_scan_and_flip(free_map, 0, 1, false);
          }
          else
          {
            single[j] = -1;
          }
        }
        buffer_write(double_table[i], single, 0, DISK_SECTOR_SIZE);
      }
    }
    for(i=entry_num_in_double; i<DISK_SECTOR_SIZE/4; i++)
    {
      double_table[i] = -1;
    }
    bitmap_write(free_map, free_map_file);
    buffer_write(double_indirect, double_table, 0, DISK_SECTOR_SIZE);
    free(single);
    free(double_table);
    return true;
  }
  return false;
}

bool
free_map_reallocate(size_t cnt, struct inode_disk * disk_inode)
{
  int i,j;
  disk_sector_t target;
  disk_sector_t single_indirect;
  disk_sector_t double_indirect;
  disk_sector_t * zeros = malloc(DISK_SECTOR_SIZE);
  if(zeros == NULL)
    return false;
  disk_sector_t unused = -1;
  memset(zeros,0,DISK_SECTOR_SIZE);

  lock_acquire(&free_map_lock);
  if(cnt<=DIRECT_NUM)
  {
    for(i=0; i<DIRECT_NUM; i++)
    {
      if(i<=cnt-1)
      {
        if(disk_inode->direct[i] == -1)
        {
          disk_inode->direct[i] = bitmap_scan_and_flip(free_map, 0, 1, false);
          buffer_write(disk_inode->direct[i], zeros, 0, DISK_SECTOR_SIZE);          
        }
      }
    }
    bitmap_write(free_map, free_map_file);
    free(zeros);
    lock_release(&free_map_lock);
    return true;
  }
  else if(cnt <= (DIRECT_NUM + DISK_SECTOR_SIZE/4))
  {
    for(i=0; i< DIRECT_NUM; i++)
    {
      if(disk_inode->direct[i] == -1)
      {
        disk_inode->direct[i] = bitmap_scan_and_flip(free_map, 0, 1, false);
        buffer_write(disk_inode->direct[i], zeros, 0, DISK_SECTOR_SIZE);
      }
    }
    

    if(disk_inode->single_indirect == -1) // we have to allocate new single_indirect inode.
    {
      single_indirect = bitmap_scan_and_flip(free_map, 0, 1, false);
      disk_inode->single_indirect = single_indirect;
      disk_sector_t * single = malloc(DISK_SECTOR_SIZE);

      for(i=0; i<DISK_SECTOR_SIZE/4; i++)
      {
        if(i< cnt - DIRECT_NUM )
        {
          target = bitmap_scan_and_flip(free_map, 0, 1, false);
          single[i] = target;
          buffer_write(target, zeros, 0, DISK_SECTOR_SIZE);
        }
        else
          single[i] = -1;
      }
      bitmap_write(free_map, free_map_file);
      buffer_write(single_indirect, single, 0, DISK_SECTOR_SIZE);
      free(single);
      free(zeros);
      lock_release(&free_map_lock);
      return true;
    }
    else // when there's already single table.
    {
      single_indirect = disk_inode->single_indirect;
      disk_sector_t * single = malloc(DISK_SECTOR_SIZE);
      buffer_read(single_indirect, single, 0, DISK_SECTOR_SIZE);

      for(i=0; i<DISK_SECTOR_SIZE/4; i++)
      {
        if(i<=cnt - DIRECT_NUM -1)
        {
          if(single[i] == -1)
          {
            single[i] = bitmap_scan_and_flip(free_map, 0, 1, false);
            buffer_write(single[i], zeros, 0, DISK_SECTOR_SIZE);
          }
        }
      }
      bitmap_write(free_map, free_map_file);
      buffer_write(single_indirect, single, 0, DISK_SECTOR_SIZE);
      free(single);
      free(zeros);
      lock_release(&free_map_lock);
      return true;
    }
  }
  else // 
  {
    size_t new_cnt = cnt - (DIRECT_NUM + DISK_SECTOR_SIZE/4); // cnt - (122 + 128), always new_cnt >= 1.
    size_t double_entry_num = DIV_ROUND_UP(new_cnt, (128 * 128));
    size_t single_entry_num = new_cnt % (128 * 128);

    if(disk_inode->double_indirect == -1) // we have to newly allocate new double_indirect sector
    {
      double_indirect = bitmap_scan_and_flip(free_map, 0, 1, false);
      disk_inode->double_indirect= double_indirect;
      
      disk_sector_t * double_table = malloc(DISK_SECTOR_SIZE);
      disk_sector_t * single_table = malloc(DISK_SECTOR_SIZE);
      for(i=0; i< double_entry_num; i++)
      {
        double_table[i] = bitmap_scan_and_flip(free_map, 0, 1, false);
        if(i!=double_entry_num-1)  // fully allocate single_indirect
        {
          for(j=0; j<DISK_SECTOR_SIZE/4; j++)
          {
            single_table[j] = bitmap_scan_and_flip(free_map, 0, 1, false);
          }
          buffer_write(double_table[i], single_table, 0, DISK_SECTOR_SIZE);
        }
        else
        {
          for(j=0; j<DISK_SECTOR_SIZE/4; j++)
          {
            if(j<single_entry_num)
            {
              single_table[j] = bitmap_scan_and_flip(free_map, 0, 1, false);
            }
            else
            {
              single_table[i] = -1;
            }
          }
          buffer_write(double_table[i], single_table, 0, DISK_SECTOR_SIZE);
        }
      }
      for(i=double_entry_num; i<DISK_SECTOR_SIZE/4; i++)
      {
        double_table[i] = -1;
      }
      bitmap_write(free_map, free_map_file);
      buffer_write(double_indirect, double_table, 0, DISK_SECTOR_SIZE);
      free(single_table);
      free(double_table);
      return true;
    }
    else // when there's already double table
    {
      double_indirect = disk_inode->double_indirect;
      disk_sector_t * double_table = malloc(DISK_SECTOR_SIZE);
      disk_sector_t * single_table = malloc(DISK_SECTOR_SIZE);
      disk_sector_t first_not_full = -5;

      buffer_read(double_indirect, double_table, 0, DISK_SECTOR_SIZE);

      for(i=0; i<double_entry_num; i++)
      {
        if(double_table[i] == -1 && i != 0)
        {
          first_not_full = i-1;
          break;
        }
      }

      if(first_not_full != -5) // we have to first not fully allocated double entry and fully allocate it.
      {
        buffer_read(first_not_full, single_table, 0, DISK_SECTOR_SIZE);
        for(j=0; j<DISK_SECTOR_SIZE/4; j++)
        {
          if(single_table[j] == -1)
          {
            single_table[j] = bitmap_scan_and_flip(free_map, 0, 1, false);
          }
        }
        buffer_write(first_not_full, single_table, 0, DISK_SECTOR_SIZE);
      }

      for(i=0; i<DISK_SECTOR_SIZE/4; i++) // initialize single table again
        single_table[i] = -1; 

      for(i=0; i<double_entry_num; i++)
      {

        if(double_table[i] == -1) // newly allocate double_table entry
        {
          double_table[i] = bitmap_scan_and_flip(free_map, 0, 1, false);

          if(i!=double_entry_num-1)
          {
            for(j=0; j<DISK_SECTOR_SIZE/4; j++)
            {
              if(single_table[j]==-1)
              {
                single_table[j] = bitmap_scan_and_flip(free_map, 0, 1, false);
              }
            }
          }
          else // in case of last double_entry. it can be not fully allocated.
          {
            for(j=0; j<single_entry_num; j++)
            {
              single_table[j] = bitmap_scan_and_flip(free_map, 0, 1, false);
            }
            for(j=single_entry_num; j<DISK_SECTOR_SIZE/4; j++)
            {
              single_table[j] = -1;
            }
          }
          buffer_write(double_table[i], single_table, 0, DISK_SECTOR_SIZE);
        }
      }
      bitmap_write(free_map, free_map_file);
      buffer_write(double_indirect, double_table, 0, DISK_SECTOR_SIZE);
      free(single_table);
      free(double_table);
      return true;
    }
  }
  return false;
}



/* Makes CNT sectors starting at SECTOR available for use. */
void
free_map_release (disk_sector_t sector, size_t cnt)
{
  ASSERT (bitmap_all (free_map, sector, cnt));
  lock_acquire(&free_map_lock);
  bitmap_set_multiple (free_map, sector, cnt, false);
  bitmap_write (free_map, free_map_file);
  lock_release(&free_map_lock);
}

/* Opens the free map file and reads it from disk. */
void
free_map_open (void) 
{
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_read (free_map, free_map_file))
    PANIC ("can't read free map");
}

/* Writes the free map to disk and closes the free map file. */
void
free_map_close (void) 
{
  file_close (free_map_file);
}

/* Creates a new free map file on disk and writes the free map to
   it. */
void
free_map_create (void) 
{
  /* Create inode. */
  if (!inode_create (FREE_MAP_SECTOR, bitmap_file_size (free_map), false, -1)) // false means creating file.
    PANIC ("free map creation failed");

  /* Write bitmap to file. */
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_write (free_map, free_map_file))
    PANIC ("can't write free map");
}
