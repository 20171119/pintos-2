#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"
#include "lib/user/syscall.h"

void syscall_init (void);

struct lock file_lock;


void check_ptr_validity(void *);
struct file_descriptor * get_fileptr(int);
struct mmap_info * get_mmapinfo(int); 
void munmap_all(void);
void halt(void);
void exit(int);
int exec(const char *);
bool create(const char *, unsigned);
bool removed(const char *);
int open(const char *);
int filesize(int);
int read(int, void *, unsigned);
int write(int, const void *, unsigned);
void seek(int, unsigned);
unsigned tell(int);
void close(int);

#ifdef VM
mapid_t mmap(int, void *);
void munmap(mapid_t);
#endif

#endif /* userprog/syscall.h */
