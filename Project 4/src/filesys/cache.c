#include "filesys/cache.h"
#include "filesys/inode.h"

void
buffer_init()
{
//  printf("buffer_init start\n");
  int i;
  struct buffcache_elem * e;

  lock_init(&buffer_lock);
  for(i=0; i<CACHE_SIZE; i++)
  {
    e = &buffer_cache[i];
    e->is_deleted = true;
    e->access = false;
    e->dirty = false;
    e->sector = -1;
  }
  hand = 0;
//  printf("buffer_init end\n");
}

int
buffer_find(disk_sector_t sector)
{
  int i;
  struct buffcache_elem * e;
  
//  printf("in buffer_find hand is %d sector is %d\n", hand, sector);
  

  for(i=0; i<CACHE_SIZE; i++)
  {
    e = &buffer_cache[i];
//    printf("i is %d, e->sector is %d\n",i, e->sector);
    if(e->sector == sector || e->is_deleted)
      return i;
  }
//  printf("for loop end\n");  
  return NO_HIT;
}

void
buffer_read(disk_sector_t sector, void * data, int offset, int size)
{
//  printf("buffer read\n");
  lock_acquire(&buffer_lock);
  int target_index = buffer_find(sector);
  struct buffcache_elem * e;

  if(target_index == NO_HIT)
  {
//    printf("in buffer_read NO_HIt\n");
    target_index = buffer_evict();
    e = &buffer_cache[target_index];

    e->sector = sector;
    e->is_deleted = false;
    e->access = false;
    e->dirty = false;
    disk_read(filesys_disk, sector, e->data);
  }
  else if(buffer_cache[target_index].is_deleted)
  {
//    printf("in buffer_read deleted \n");
    e = &buffer_cache[target_index];

    e->sector = sector;
    e->is_deleted = false;
    e->access = false;
    e->dirty = false;
    disk_read(filesys_disk, sector, e->data);
  }
  else // cache hit
  {
//    printf("cache hit\n");
    e = &buffer_cache[target_index];
  }
  e->access = true;
  memcpy(data, e->data + offset, size);
  lock_release(&buffer_lock);

}

void
buffer_write(disk_sector_t sector, void * data, int offset, int size)
{
//  printf("buffer_write\n");
  lock_acquire(&buffer_lock);
  int target_index = buffer_find(sector);
  struct buffcache_elem * e;


  if(target_index == NO_HIT)
  {
//    printf("in case of NO_HIT in buffer_write\n");
    target_index = buffer_evict();
    e = &buffer_cache[target_index];
    disk_read(filesys_disk, sector, e->data);

    e->sector = sector;
    e->is_deleted = false;
    e->access = false;
    e->dirty = false;
  }
  else if(buffer_cache[target_index].is_deleted)
  {
//    printf("in case of deleted in buffer_write\n");
    e = &buffer_cache[target_index];
    disk_read(filesys_disk, sector, e->data);

    e->sector = sector;
    e->is_deleted = false;
    e->access = false;
    e->dirty = false;
  }
  else
  {
//    printf("cache hit\n");
    e = &buffer_cache[target_index];
  }

  e->dirty = true;
  e->access = true;

  memcpy(e->data + offset, data, size);
  //struct inode_disk * k = (struct inode_disk *)data;
  //printf("k->start is %d, k->length is %d\n", ((struct inode_disk *)data)->start,((struct inode_disk *)data)->length);


  e = &buffer_cache[target_index];
  //struct inode_disk * a = (struct inode_disk *) &e->data;
  //printf("a->start is %d, a->length is %d\n", a->start, a->length);

  lock_release(&buffer_lock);
}

void
buffer_flush_all()
{
  int i;
  struct buffcache_elem * e;
  lock_acquire(&buffer_lock);

  for(i=0; i<CACHE_SIZE; i++)
  {
    e = &buffer_cache[i];
    if(e->dirty)
      disk_write(filesys_disk, e->sector, e->data);
  }
  lock_release(&buffer_lock);
}

int buffer_evict()
{
  int i;
  struct buffcache_elem * e;
  int result = -1;
  
  //printf("buffer_evict called ");

  while(true)
  {
    e = &buffer_cache[hand];
    if(e->access)
    {
      e->access = false;
      hand = (hand+1)%CACHE_SIZE;
    }
    else // do evict
    {
      //printf("evicted sector is %d\n", e->sector);
      if(e->dirty)
      {
        disk_write(filesys_disk, e->sector, e->data);
      }
      //disk_write(filesys_disk, e->sector, e->data);
      result = hand;
      hand = (hand+1)%CACHE_SIZE;
      break;
    }
  }

//  printf("buffer_evict end\n");

  return result;
}
























