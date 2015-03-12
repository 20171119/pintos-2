#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"

struct bitmap;

#define DIRECT_NUM (122)
#define SINGLE_INDIRECT_START (DISK_SECTOR_SIZE * DIRECT_NUM)
#define DOUBLE_INDIRECT_START (SINGLE_INDIRECT_START + DISK_SECTOR_SIZE * DISK_SECTOR_SIZE / 4)

enum inode_type
{
  INODE_FILE,
  INODE_DIR,
};

struct inode_disk
{
  off_t length;
  enum inode_type type;
  unsigned magic;
//  unsigned unused;
  disk_sector_t parent;
  disk_sector_t direct[122];
  disk_sector_t single_indirect;
  disk_sector_t double_indirect;
};


void inode_init (void);
//bool inode_create (disk_sector_t, off_t, bool); // original: inode_create(disk_sector_t, off_t);
bool inode_create(disk_sector_t, off_t, bool, disk_sector_t);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
disk_sector_t inode_get_parent (struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
bool inode_is_removed(struct inode *); // newly added
bool inode_is_dir(struct inode * inode); //newly added
#endif /* filesys/inode.h */
