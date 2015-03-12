#ifndef FRAME_H
#define FRAME_H

#include "threads/synch.h"
#include <list.h>

struct fte
{
  struct list_elem elem;
  void * pagedir;
  void * uaddr;
  void * kpage;
  int tid;
  bool completed;
};

struct list frame_table;
struct lock frame_lock;

void frame_init(void);
struct fte * frame_find(void *, int, bool);
void * frame_alloc(void);
void frame_free(struct fte *);
void frame_set_uaddr(struct fte *, void *);
void frame_set_complete(struct fte *);
void frame_clear(struct fte *);
void frame_free_tid(int);
struct fte * frame_select_evict(void);

#endif
