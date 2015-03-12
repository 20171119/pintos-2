#include <stdio.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include <list.h>
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/vaddr.h"

void
spt_init(struct thread * curr_thread)
{
  printf("in spt_init\n");
  list_init(&curr_thread->supple_table);
}

void
spt_add(struct spte * spt_entry)
{
//  printf("spt_entry->uaddr = %p\n", spt_entry->uaddr);
  struct thread * curr_thread = thread_current();
  struct list * supple_table = &curr_thread->supple_table;

  list_push_back(supple_table, &spt_entry->elem);
//  printf("supple table size is %d\n", list_size(supple_table));
}

void
spt_delete(struct spte * spt_entry)
{
  struct list * supple_table = &thread_current()->supple_table;
  list_remove(&spt_entry->elem);
  free(spt_entry);
}

bool
spt_create_PRESENT(void * uaddr, void * kpage, bool writable)
{
  struct spte * spt_entry = malloc(sizeof (struct spte));
  if(spt_entry == NULL)
  {
    printf("malloc failed in spt_create_PRESENT. spt_entry is NULL\n");
    return false;
  }

  spt_entry->type = SPT_PRESENT;
  spt_entry->sec_no = -1;
  spt_entry->kpage = kpage;
  spt_entry->uaddr = pg_round_down(uaddr);
  spt_entry->writable = writable;
  spt_entry->file = NULL;
  spt_entry->offset = -1;
  spt_entry->read_bytes = -1;
  spt_entry->zero_bytes = -1;

  spt_add(spt_entry);
  return true;
}

bool
spt_create_FILE(void * uaddr, struct file * f, int offset, int read_bytes, int zero_bytes, bool writable)
{
  struct spte * spt_entry = malloc(sizeof (struct spte));
  if(spt_entry == NULL)
  {
    printf("malloc failed in spt_create_FILE. spt_entry is NULL\n");
    return false;
  }

  spt_entry->type = SPT_FILE;
  spt_entry->sec_no = -1;
  spt_entry->kpage = NULL;
  spt_entry->uaddr = pg_round_down(uaddr);
  spt_entry->writable = writable;
  spt_entry->file = f;
  spt_entry->offset = offset;
  spt_entry->read_bytes = read_bytes;
  spt_entry->zero_bytes = zero_bytes;

  spt_add(spt_entry);
  return true;
}

bool
spt_create_MMAP(void * uaddr, struct file * f, int offset, int read_bytes, int zero_bytes, bool writable)
{
  struct spte * spt_entry = malloc(sizeof (struct spte));
  if(spt_entry == NULL)
  {
    printf("malloc failed in spt_create_MMAP. spt_entry is NULL\n");
    return false;
  }

  spt_entry->type = SPT_MMAP;
  spt_entry->sec_no = -1;
  spt_entry->kpage = NULL;
  spt_entry->uaddr = pg_round_down(uaddr);
  spt_entry->writable = writable;
  spt_entry->file = f;
  spt_entry->offset = offset;
  spt_entry->read_bytes = read_bytes;
  spt_entry->zero_bytes = zero_bytes;

  spt_add(spt_entry);
  return true;
}

struct
spte * spt_find(void * uaddr, int tid)
{
  struct list_elem * e;
  struct spte * spt_entry;
  struct thread * target_thread = get_thread(tid);
  struct list * supple_table = &target_thread->supple_table;
  void * target_uaddr = pg_round_down(uaddr);

  if(list_empty(supple_table))
      return NULL;
  for(e=list_begin(supple_table); e!=list_end(supple_table); e=list_next(e))
  {
    spt_entry = list_entry(e, struct spte, elem);
    if(spt_entry->uaddr == target_uaddr)
      return spt_entry;
  }

  return NULL;
}

void
spt_exit()
{
  struct list_elem * e;
  struct spte * spt_entry;
  struct list * supple_table = &thread_current()->supple_table;

  while(!list_empty(supple_table))
  {
    e = list_begin(supple_table);
    spt_entry = list_entry(e, struct spte, elem);
    spt_delete(spt_entry);
  }
}

