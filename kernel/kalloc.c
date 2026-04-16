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

/*
Lab: lock
- per-CPU freelist to reduce lock contention
- steal pages from other CPUs when local freelist is empty

Lab: cow
- physical page reference count must be global, not per-CPU

Lab: sysinfo/free_mem
- count total free pages from all per-CPU freelists
*/

#define NPAGES ((PHYSTOP - KERNBASE) / PGSIZE)

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

struct {
  struct spinlock lock;
  int cnt[NPAGES];
} kref;

static inline int
pa2index(uint64 pa)
{
  return (pa - KERNBASE) / PGSIZE;
}

void
krefinit(void)
{
  initlock(&kref.lock, "kref");
  for(int i = 0; i < NPAGES; i++){
    // initialize to 1 so that freerange()->kfree()
    // will decrement to 0 and really put the page
    // into freelist.
    kref.cnt[i] = 1;
  }
}

void
kinit(void)
{
  for(int i = 0; i < NCPU; i++){
    initlock(&kmem[i].lock, "kmem");
    kmem[i].freelist = 0;
  }

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

// steal exactly one page from other CPU freelist.
// returns 0 if no page can be stolen.
// https://leetcode.cn/problems/delete-the-middle-node-of-a-linked-list
static struct run*
steal_from_other_cpu(int self)
{
  for(int i = 0; i < NCPU; i++){
    if(i == self)
      continue;

    acquire(&kmem[i].lock);
    if(kmem[i].freelist){
      struct run *slow = kmem[i].freelist;
      struct run *fast = kmem[i].freelist;
      struct run *prev = 0;

      while(fast && fast->next){
        prev = slow;
        slow = slow->next;
        fast = fast->next->next;
      }

      // donate points to the second half
      struct run *donate;
      if(prev == 0){
        // only one node
        donate = kmem[i].freelist;
        kmem[i].freelist = 0;
      } else {
        donate = slow;
        prev->next = 0;
      }

      release(&kmem[i].lock);
      return donate;
    }
    release(&kmem[i].lock);
  }

  return 0;
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc(). (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int idx;
  int id;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  idx = pa2index((uint64)pa);

  // First handle the global reference count.
  acquire(&kref.lock);
  if(kref.cnt[idx] <= 0){
    release(&kref.lock);
    panic("kfree ref");
  }

  kref.cnt[idx]--;
  if(kref.cnt[idx] > 0){
    // still referenced by someone else
    release(&kref.lock);
    return;
  }
  release(&kref.lock);

  // No references left: really free the page.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // Put the page into current CPU's freelist.
  push_off();
  id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r = 0;
  struct run *donate = 0;
  int id;
  int idx;

  push_off();
  id = cpuid();
  pop_off();

  // fast path: try local freelist first
  acquire(&kmem[id].lock);
  if(kmem[id].freelist){
    r = kmem[id].freelist;
    kmem[id].freelist = r->next;
    release(&kmem[id].lock);
  } else {
    // release self's lock before occupy other's lock in function steal_from_other_cpu()
    // avoid hold and wait
    // avoid deadlock ?
    release(&kmem[id].lock);

    // local empty, steal half from others
    donate = steal_from_other_cpu(id);

    acquire(&kmem[id].lock);
    if(donate){
      kmem[id].freelist = donate;
      r = kmem[id].freelist;
      kmem[id].freelist = r->next;
    }
    release(&kmem[id].lock);
  }

  if(r){
    idx = pa2index((uint64)r);
    acquire(&kref.lock);
    kref.cnt[idx] = 1;
    release(&kref.lock);

    memset((char*)r, 5, PGSIZE);
    r->next = 0;
  }

  return (void*)r;
}

// lab2: count total free memory in all freelists.
uint64
free_mem(void)
{
  uint64 free_pages_count = 0;

  for(int i = 0; i < NCPU; i++){
    acquire(&kmem[i].lock);
    struct run *cur = kmem[i].freelist;
    while(cur){
      free_pages_count++;
      cur = cur->next;
    }
    release(&kmem[i].lock);
  }

  return free_pages_count * PGSIZE;
}

// lab cow: increase reference count of physical page pa.
void
kaddref(uint64 pa)
{
  int idx = pa2index(pa);

  acquire(&kref.lock);
  kref.cnt[idx]++;
  release(&kref.lock);
}

// lab cow: decrease reference count of physical page pa.
// usually kfree() is the preferred path for actual freeing;
// this helper only adjusts the count.
void
ksubref(uint64 pa)
{
  int idx = pa2index(pa);

  acquire(&kref.lock);
  if(kref.cnt[idx] <= 0){
    release(&kref.lock);
    panic("ksubref");
  }
  kref.cnt[idx]--;
  release(&kref.lock);
}

// lab cow: get reference count of physical page pa.
int
kgetref(uint64 pa)
{
  int idx = pa2index(pa);
  int n;

  acquire(&kref.lock);
  n = kref.cnt[idx];
  release(&kref.lock);

  return n;
}