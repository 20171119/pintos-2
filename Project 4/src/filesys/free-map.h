#ifndef FILESYS_FREE_MAP_H
#define FILESYS_FREE_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/disk.h"
#include "threads/synch.h"

struct lock free_map_lock;

void free_map_init (void);
void free_map_read (void);
void free_map_create (void);
void free_map_open (void);
void free_map_close (void);

bool free_map_allocate_one(disk_sector_t *);
//bool free_map_allocate (size_t, disk_sector_t *);
bool free_map_reallocate(size_t, struct inode_disk *);
bool free_map_allocate(size_t, struct inode_disk *);
void free_map_release (disk_sector_t, size_t);

#endif /* filesys/free-map.h */
