#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include <stdbool.h>
#include "devices/disk.h"
#include "filesys/off_t.h"
#include "threads/synch.h"
#include "filesys/filesys.h"

#define CACHE_SIZE 64
#define NO_HIT (-2)

struct buffcache_elem
{
  disk_sector_t sector;
  char data[DISK_SECTOR_SIZE];
  bool is_deleted;
  bool access;
  bool dirty;
};

struct buffcache_elem buffer_cache[CACHE_SIZE];
struct lock buffer_lock;
int hand;

void buffer_init(void);
void buffer_done(void);
int buffer_find(disk_sector_t);
void buffer_read(disk_sector_t, void *, int, int);
void buffer_write(disk_sector_t, void *, int, int);
void buffer_flush_all(void);
int buffer_evict(void);

#endif
