#ifndef PAGE_H
#define PAGE_H

#include "filesys/file.h"
#include "threads/thread.h"

enum spte_type
{
  SPT_PRESENT,
  SPT_SWAP,
  SPT_FILE,
  SPT_MMAP,
};

/* supplementary page table entry */
struct spte
{
  enum spte_type type;
  int sec_no;
  void * kpage;
  void * uaddr;
  bool writable;

  struct file * file;
  int offset;
  int read_bytes;
  int zero_bytes;

  struct list_elem elem;
};

void spt_init(struct thread *);
void spt_add(struct spte *);
void spt_delete(struct spte *);
bool spt_create_PRESENT(void *, void *, bool);
bool spt_create_FILE(void *, struct file *, int, int, int, bool);
bool spt_create_MMAP(void *, struct file *, int, int, int, bool);
struct spte * spt_find(void * uaddr, int tid);
void spt_exit(void);
#endif
