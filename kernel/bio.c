// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define HASH(dev, blockno) (((dev) + (blockno)) % NBUCKET)

// 全局多个链表
struct bucket {
  struct spinlock lock;
  struct buf head;  // 哨兵节点，挂这个桶里面的buf
};

struct {
  struct buf buf[NBUF];
  struct bucket buckets[NBUCKET];
} bcache;

// remove b from its current doubly-linked list.
// caller must hold the corresponding bucket lock.
static void
buf_remove(struct buf *b)
{
  b->prev->next = b->next;
  b->next->prev = b->prev;
  b->next = 0;
  b->prev = 0;
}

// insert b right after head in doubly-linked list.
// caller must hold the corresponding bucket lock.
static void
buf_insert_head(struct buf *head, struct buf *b)
{
  b->next = head->next;
  b->prev = head;
  head->next->prev = b;
  head->next = b;
}

void
binit(void)
{
  struct buf *b;
  // init lock
  for(int i=0;i<NBUCKET;i++){
    initlock(&bcache.buckets[i].lock,"bache.bucket");
    bcache.buckets[i].head.prev=&bcache.buckets[i].head;
    bcache.buckets[i].head.next=&bcache.buckets[i].head;
  }
  // init all bufs and dispatch to buckets
  for(int i=0;i<NBUF;i++){
    b=&bcache.buf[i];
    initsleeplock(&b->lock,"buffer");

    b->valid=0;
    b->disk=0;
    b->dev=0;
    b->blockno=0;
    b->refcnt=0;

    int hash=i%NBUCKET;
    // insert after bucket head
    b->next=bcache.buckets[hash].head.next;
    b->prev=&bcache.buckets[hash].head;
    // 让哨兵节点的dummy head的next指向b
    bcache.buckets[hash].head.next->prev=b; // b -> prev = b
    bcache.buckets[hash].head.next=b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int h = HASH(dev, blockno);

  while(1){
    // 1) lookup only in target bucket
    acquire(&bcache.buckets[h].lock);
    for(b = bcache.buckets[h].head.next; b != &bcache.buckets[h].head; b = b->next){
      if(b->dev == dev && b->blockno == blockno){
        b->refcnt++;
        release(&bcache.buckets[h].lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.buckets[h].lock);

    // 2) miss: scan buckets to steal one unused buf
    struct buf *victim = 0;
    int victim_bucket = -1;

    for(int i = 0; i < NBUCKET; i++){
      acquire(&bcache.buckets[i].lock);
      for(b = bcache.buckets[i].head.next; b != &bcache.buckets[i].head; b = b->next){
        if(b->refcnt == 0){
          victim = b;
          victim_bucket = i;
          buf_remove(victim);
          release(&bcache.buckets[i].lock);
          goto found_victim;
        }
      }
      release(&bcache.buckets[i].lock);
    }

    panic("bget: no buffers");

found_victim:
    // check then add
    // 系统可能会出现多条执行流，那么只要你在判断之后做操作了，其他的core就会借机插上，修改buf的状态，所以要recheck
    // 3) re-check target bucket before inserting, to avoid duplicates
    acquire(&bcache.buckets[h].lock);
    for(b = bcache.buckets[h].head.next; b != &bcache.buckets[h].head; b = b->next){
      if(b->dev == dev && b->blockno == blockno){
        b->refcnt++;
        release(&bcache.buckets[h].lock);

        // someone else inserted it while we were stealing.
        // put victim back to its original bucket.
        acquire(&bcache.buckets[victim_bucket].lock);
        buf_insert_head(&bcache.buckets[victim_bucket].head, victim);
        release(&bcache.buckets[victim_bucket].lock);

        acquiresleep(&b->lock);
        return b;
      }
    }

    // 4) install victim into target bucket
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    victim->refcnt = 1;
    buf_insert_head(&bcache.buckets[h].head, victim);
    release(&bcache.buckets[h].lock);

    acquiresleep(&victim->lock);
    return victim;
  }
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  int h;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  h = HASH(b->dev, b->blockno);
  acquire(&bcache.buckets[h].lock);
  b->refcnt--;
  if(b->refcnt < 0)
    panic("brelse");
  // no global LRU maintenance in this bucketed version
  release(&bcache.buckets[h].lock);
}

void
bpin(struct buf *b) {
  int h = HASH(b->dev, b->blockno);
  acquire(&bcache.buckets[h].lock);
  b->refcnt++;
  release(&bcache.buckets[h].lock);
}

void
bunpin(struct buf *b) {
  int h = HASH(b->dev, b->blockno);
  acquire(&bcache.buckets[h].lock);
  b->refcnt--;
  if(b->refcnt<0)
    panic("bunpin");
  release(&bcache.buckets[h].lock);
}


