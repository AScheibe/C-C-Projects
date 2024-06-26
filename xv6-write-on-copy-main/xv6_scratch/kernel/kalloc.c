// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "spinlock.h"

#define PGNUM(pa)   ((uint)pa) / PGSIZE;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;

  /*
  P5 changes
  */
  uint free_pages; //track free pages
  uint ref_cnt[PHYSTOP / PGSIZE]; //track reference count

} kmem;

extern char end[]; // first address after kernel loaded from ELF file

int getFreePagesKalloc(void){
  acquire(&kmem.lock);

  int free_pages = kmem.free_pages;
  
  release(&kmem.lock);
  
  return free_pages;
}

// Initialize free list of physical pages.
void
kinit(void)
{
  char *p;

  initlock(&kmem.lock, "kmem");

  acquire(&kmem.lock);
  kmem.free_pages = 0;
  for (int i = 0; i < PHYSTOP / PGSIZE; i++) {
    kmem.ref_cnt[i] = 1;
  }
  release(&kmem.lock);


  p = (char*)PGROUNDUP((uint)end);
  for(; p + PGSIZE <= (char*)PHYSTOP; p += PGSIZE)
    kfree(p);
  
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || (uint)v >= PHYSTOP)
    panic("kfree");

  int page_num = PGNUM(v);

  acquire(&kmem.lock);
  if (kmem.ref_cnt[page_num] > 1) {
    kmem.ref_cnt[page_num]--;
    release(&kmem.lock);
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);
  kmem.free_pages++;

  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.ref_cnt[page_num]--;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
  }
    

  int page_num = PGNUM(r);
  kmem.ref_cnt[page_num] = 1;
  kmem.free_pages--;  
  release(&kmem.lock);
  return (char*)r;
}

// increment ref count for page
void
kincrement(char *v) {
  if((uint)v % PGSIZE || v < end || (uint)v >= PHYSTOP)
    panic("kincrement");

  int page_num = PGNUM(v);
  acquire(&kmem.lock);
  kmem.ref_cnt[page_num]++;
  release(&kmem.lock);
}

int
kgetrefcnt(char *v) {
  if((uint)v % PGSIZE || v < end || (uint)v >= PHYSTOP) {
    panic("kgetrefcnt");
  }

  int page_num = PGNUM(v);
  acquire(&kmem.lock);
  int retval = kmem.ref_cnt[page_num];
  release(&kmem.lock);
  return retval;
}

void
kdecrement(char *v) {
  if((uint)v % PGSIZE || v < end || (uint)v >= PHYSTOP)
    panic("kdecrement");

  int page_num = PGNUM(v);
  acquire(&kmem.lock);
  kmem.ref_cnt[page_num]--;
  release(&kmem.lock);
}
