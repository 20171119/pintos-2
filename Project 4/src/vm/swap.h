#ifndef SWAP_H
#define SWAP_H
#include "threads/synch.h"
#include "devices/disk.h"
#include <bitmap.h>

#define SECTOR_NUMBER_PER_PAGE 8

struct bitmap * swap_table;
struct lock swap_lock;
struct disk * swap_disk;

void swap_init(void);
disk_sector_t swap_sector_alloc(void);
void swap_out(void *, int);
void swap_in(void *);
void swap_out_one_frame(void);

#endif
