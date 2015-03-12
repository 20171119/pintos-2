#include "vm/frame.h"
#include <stdio.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "filesys/file.h"

void
frame_init()
{
  list_init(&frame_table);
  lock_init(&frame_lock);
}

struct fte *
frame_find(void * vaddr, int tid, bool is_kernel)
{

  struct list_elem * e;
  struct fte * frame_entry;
  void * target = pg_round_down(vaddr);

  if(list_empty(&frame_table))
    return false;

  if(is_kernel)
  {
    lock_acquire(&frame_lock);
    for(e=list_begin(&frame_table); e!=list_end(&frame_table); e=list_next(e))
    {
      frame_entry = list_entry(e, struct fte, elem);
      if(frame_entry->kpage == target && frame_entry->tid == tid)
      {
        lock_release(&frame_lock);
        return frame_entry;
      }
    }
    lock_release(&frame_lock);
  }
  else
  {
    lock_acquire(&frame_lock);
    for(e=list_begin(&frame_table); e!=list_end(&frame_table); e=list_next(e))
    {
      frame_entry = list_entry(e, struct fte, elem);
      if(frame_entry->uaddr == target && frame_entry->tid == tid)
      {
        lock_release(&frame_lock);
        return frame_entry;
      }
    }
    lock_release(&frame_lock);
  }
  return NULL;
}

void *
frame_alloc()
{
  lock_acquire(&frame_lock);

  void * kpage = palloc_get_page(PAL_USER);
  
  if(kpage == NULL)
  {
    lock_release(&frame_lock);
    return NULL;
  }
  struct fte * frame_entry = malloc(sizeof(struct fte));
  if(frame_entry == NULL)
  {
    printf("frame_entry is NULL. malloc failed\n");
    palloc_free_page(kpage);
    lock_release(&frame_lock);
    return NULL;
  }

  struct thread * curr_thread = thread_current();

  frame_entry->pagedir = curr_thread->pagedir;
  frame_entry->uaddr = NULL;
  frame_entry->kpage = kpage;
  frame_entry->tid = curr_thread->tid;
  frame_entry->completed = false;
//  lock_acquire(&frame_lock);
  list_push_back(&frame_table, &frame_entry->elem);
  lock_release(&frame_lock);

  return kpage;
}

void
frame_free(struct fte * frame_entry)
{
  list_remove(&frame_entry->elem);
  palloc_free_page(frame_entry->kpage);
  free(frame_entry);
}

void
frame_set_uaddr(struct fte * frame_entry, void * uaddr)
{
  lock_acquire(&frame_lock);
  frame_entry->uaddr = uaddr;
  lock_release(&frame_lock);
}


void
frame_set_complete(struct fte * frame_entry)
{

  lock_acquire(&frame_lock);
  frame_entry->completed = true;
  lock_release(&frame_lock);
}

void
frame_clear(struct fte * frame_entry)
{
  if(frame_entry->uaddr == NULL)
  {
    printf("frame_entry->uaddr is NULL\n");
    return;
  }
  pagedir_clear_page(frame_entry->pagedir, frame_entry->uaddr);
  
  frame_free(frame_entry);
}

void
frame_free_tid(int tid)  // called when process exits. MUST not palloc_free_page because it is done by pagedir_destroy.
{
  struct list_elem * e;
  struct fte * frame_entry;

  lock_acquire(&frame_lock);
  e = list_begin(&frame_table);
  while(e!=list_tail(&frame_table))
  {
    frame_entry = list_entry(e, struct fte, elem);
    e = list_next(e);
    
    if(frame_entry->tid == tid)
    {
      list_remove(&frame_entry->elem);
      free(frame_entry);
    }

  }
  lock_release(&frame_lock);
}

struct fte *
frame_select_evict()
{
  struct list_elem * e;
  struct fte * frame_entry;

  if(!lock_held_by_current_thread(&frame_lock))
    lock_acquire(&frame_lock);
  
  for(e=list_begin(&frame_table); e!=list_end(&frame_table); e=list_next(e))
  {
    frame_entry = list_entry(e, struct fte, elem);

    if(frame_entry->completed == true && frame_entry->tid == thread_current()->tid)
    {
      lock_release(&frame_lock);
      return frame_entry;
    }
  }
  
  for(e=list_end(&frame_table); e!=list_begin(&frame_table); e=list_prev(e))
  {
    frame_entry = list_entry(e, struct fte, elem);
    
    if(frame_entry->completed == true)
    {
      lock_release(&frame_lock);
      return frame_entry;
    }
  }
  
  lock_release(&frame_lock);
  return NULL;

}
