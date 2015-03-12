#include "threads/vaddr.h"
#include "threads/thread.h"
#include <stdio.h>
#include "vm/swap.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include "threads/interrupt.h"

void
swap_init(void)
{
  swap_disk = disk_get(1,1);
  int swapdisk_size = disk_size(swap_disk);
  lock_init(&swap_lock);
  swap_table = bitmap_create(swapdisk_size);
  bitmap_set_all(swap_table, false);
}

disk_sector_t
swap_sector_alloc()
{
  lock_acquire(&swap_lock);
  int free_index = bitmap_scan_and_flip(swap_table, 0, SECTOR_NUMBER_PER_PAGE, false);
  lock_release(&swap_lock);
  
  return free_index;
}

void
swap_out(void * uaddr, int tid)
{
  struct fte * frame_entry = frame_find(uaddr, tid, false);
  if(frame_entry == NULL)
  {
    printf("frame_entry is NULL in swap_out\n");
    return;
  }

  struct spte * spt_entry = spt_find(uaddr, tid);
  if(spt_entry == NULL)
  {
    printf("spt_entry is NULL in swap_out\n");
    return;
  }

  int i;
  int target_index = swap_sector_alloc();
  if(target_index == BITMAP_ERROR)
  {
    printf("target_index == BITMAP_ERROR in swap_out\n");
    return;
  }


  for(i=0; i<SECTOR_NUMBER_PER_PAGE; i++)
  {
    disk_write(swap_disk, target_index + i, frame_entry->kpage + i*DISK_SECTOR_SIZE);  // don't use disk_write(.... , target_uaddr + i*DISK_SECTOR_SIZE). 
  }                                                                                    // if a current thread swaps out other thread's frame, given uaddr mapped to current thread's uaddr, which is wrong. so we use kpage. 
  
  lock_acquire(&frame_lock);
  frame_clear(frame_entry);
  lock_release(&frame_lock);

  spt_entry->type = SPT_SWAP;
  spt_entry->sec_no = target_index;
  spt_entry->kpage = NULL;
  return;

}

void
swap_in(void * uaddr)
{
  int i;
  struct spte * spt_entry = spt_find(uaddr, thread_current()->tid);
  void * target_uaddr = pg_round_down(uaddr);

  if(spt_entry == NULL)
  {
    printf("spt_entry is NULL in swap_in\n");
    return;
  }
  if(spt_entry->type != SPT_SWAP)
  {
    printf("spt_entry->type is not SPT_SWAP\n");
    return;
  }
  
  int target_index = spt_entry->sec_no;
  void * kpage = frame_alloc();
  
  if(kpage)
  {
    struct fte * frame_entry = frame_find(kpage, thread_current()->tid, true);
    if(frame_entry == NULL)
    {
      printf("frame_entry is NULL in swap_in\n");
      return;
    }
    
    for(i=0; i<SECTOR_NUMBER_PER_PAGE; i++)
    {
      disk_read(swap_disk, target_index + i, kpage + i*DISK_SECTOR_SIZE);
    }

    /* bitmap flip */
    lock_acquire(&swap_lock);
    bitmap_set_multiple(swap_table, spt_entry->sec_no, SECTOR_NUMBER_PER_PAGE, false);
    lock_release(&swap_lock);

    frame_set_uaddr(frame_entry, target_uaddr);
    pagedir_set_page(thread_current()->pagedir, target_uaddr, kpage, spt_entry->writable);
   
    spt_entry->type = SPT_PRESENT;
    spt_entry->uaddr = target_uaddr;
    spt_entry->kpage = kpage;
    spt_entry->sec_no = -1;
  }
  else
  {
    swap_out_one_frame();
    kpage = frame_alloc();
    
    if(kpage == NULL)
    {
      printf("mangham\n");
      return;
    }
    struct fte * frame_entry = frame_find(kpage, thread_current()->tid, true);

    for(i=0; i<SECTOR_NUMBER_PER_PAGE; i++)
    {
      disk_read(swap_disk, target_index + i, kpage + i*DISK_SECTOR_SIZE);
    }

    /* bitmap flip */
    lock_acquire(&swap_lock);
    bitmap_set_multiple(swap_table, spt_entry->sec_no, SECTOR_NUMBER_PER_PAGE, false);
    lock_release(&swap_lock);

    frame_set_uaddr(frame_entry, target_uaddr);
    pagedir_set_page(thread_current()->pagedir, target_uaddr, kpage, spt_entry->writable);
  
    spt_entry->type = SPT_PRESENT;
    spt_entry->uaddr = target_uaddr;
    spt_entry->kpage = kpage;
    spt_entry->sec_no = -1;
  }
  
  return;

}

void
swap_out_one_frame()
{
  struct fte * evicted_frame = frame_select_evict();
  swap_out(evicted_frame->uaddr, evicted_frame->tid);
}



