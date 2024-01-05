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

#define NBUCKET 13

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct spinlock bucketlock[NBUCKET];
  struct buf bucket[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for (int i = 0; i < NBUCKET; ++i) {
    initlock(&bcache.bucketlock[i], "bcache");
    bcache.bucket[i].prev = &bcache.bucket[i];
    bcache.bucket[i].next = &bcache.bucket[i];
  }

  int n = 0;
  for (b = bcache.buf; b < bcache.buf+NBUF; ++b) {
    b->next = bcache.bucket[n].next;
    b->prev = &bcache.bucket[n];
    initsleeplock(&b->lock, "buffer");
    bcache.bucket[n].next->prev = b;
    bcache.bucket[n].next = b;
    ++n;
    n %= NBUCKET;
  }

}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int n = blockno % NBUCKET;
  acquire(&bcache.bucketlock[n]);
  for (b = bcache.bucket[n].next; b != &bcache.bucket[n]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucketlock[n]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for (b = bcache.bucket[n].next; b != &bcache.bucket[n]; b = b->next) {
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.bucketlock[n]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for (int i = 0; i < NBUCKET; ++i) {
    if (i == n)
      continue;
    acquire(&bcache.bucketlock[i]);
    for (b = bcache.bucket[i].next; b != &bcache.bucket[i]; b = b->next) {
      if (b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache.bucket[n].next;
        b->prev = &bcache.bucket[n];
        bcache.bucket[n].next->prev = b;
        bcache.bucket[n].next = b;
        release(&bcache.bucketlock[i]);
        release(&bcache.bucketlock[n]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.bucketlock[i]);
  }
  panic("bget: no buffers");
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
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int n = b->blockno % NBUCKET;

  acquire(&bcache.bucketlock[n]);
  b->refcnt--;
  release(&bcache.bucketlock[n]);
}

void
bpin(struct buf *b) {
  int n = b->blockno % NBUCKET;
  acquire(&bcache.bucketlock[n]);
  b->refcnt++;
  release(&bcache.bucketlock[n]);
}

void
bunpin(struct buf *b) {
  int n = b->blockno % NBUCKET;
  acquire(&bcache.bucketlock[n]);
  b->refcnt--;
  release(&bcache.bucketlock[n]);
}


