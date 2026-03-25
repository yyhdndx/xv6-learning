// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  int ref_cnt[(PHYSTOP-KERNBASE)/PGSIZE]; // lab5 COW
  // 维护子进程页表对父进程页表的引用
} kmem;

void
krefinit(void)
{
  for(int i = 0; i < (PHYSTOP-KERNBASE)/PGSIZE; i++)
    kmem.ref_cnt[i] = 1; // 初始设成1的目的是让freerange中的kfree顺利-1
}

int
pa2index(uint64 pa)
{
  return (pa - KERNBASE) / PGSIZE;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  krefinit();
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  r = (struct run*)pa;

  // reduce the count of ref until ref_cnt turn into the 0
  acquire(&kmem.lock);
  int idx=pa2index((uint64)pa);
  if(kmem.ref_cnt[idx]<=0){
    panic("kfree ref");
  }
  kmem.ref_cnt[idx]--;
  if(kmem.ref_cnt[idx]>0){
    // 仍然存在对这个页表有引用的进程，不能free，直接返回
    release(&kmem.lock);
    return ;
  }
  release(&kmem.lock);
  
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  // put the free page to the front of the list 
  // use lock to make sure atomic
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // acquire the free page from the front of the freelist
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;

  if(r){
    kmem.ref_cnt[pa2index((uint64)r)]=1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

uint64 free_mem(void){
  acquire(&kmem.lock);
  uint64 free_pages_count=0;
  struct run* r=kmem.freelist;
  while(r){
    ++free_pages_count;
    r=r->next;
  }
  release(&kmem.lock);
  return free_pages_count*PGSIZE;
}

void kaddref(uint64 pa){
  acquire(&kmem.lock);
  kmem.ref_cnt[pa2index(pa)]++;
  release(&kmem.lock);
}

void ksubref(uint64 pa){
  acquire(&kmem.lock);
  int idx=pa2index(pa);
  if(kmem.ref_cnt[idx]<=0){
    panic("ksubref");
  }
  kmem.ref_cnt[idx]--;
  release(&kmem.lock);
}

int kgetref(uint64 pa){
  acquire(&kmem.lock);
  int n=kmem.ref_cnt[pa2index(pa)];
  release(&kmem.lock);
  return n;
}
