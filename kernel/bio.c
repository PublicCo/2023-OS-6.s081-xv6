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

struct
{

  struct buf buf[NBUF];
  // 拆成几个bucket
  struct spinlock lock[NBUCKET];
  struct buf head[NBUCKET];

  // 大锁
  struct spinlock biglock;

} bcache;

int hash(int num)
{
  return num % NBUCKET;
}
void binit(void)
{
  struct buf *b;

  initlock(&bcache.biglock, "biglock");
  for (int i = 0; i < NBUCKET; i++)
  {
    initlock(&bcache.lock[i], "lock");
  }

  for (int i = 0; i < NBUCKET; i++)
  {
    bcache.head[i].next = &bcache.head[i];
    bcache.head[i].prev = &bcache.head[i];
  }

  // 全放到第0个
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    b->next = bcache.head[0].next;
    b->prev = &bcache.head[0];
    initsleeplock(&b->lock, "buffer");
    bcache.head[0].next->prev = b;
    bcache.head[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;
  int time = 0;
  struct buf *SelectFreeBlock = 0;
  int hashkey = hash(blockno);
  acquire(&bcache.lock[hashkey]);

  // Is the block already cached?
  // 如果cached了，直接返回
  for (b = bcache.head[hashkey].next; b != &bcache.head[hashkey]; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&bcache.lock[hashkey]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[hashkey]);

  // Not cached.
  // 避免死锁，先大锁稳住再上小锁
  acquire(&bcache.biglock);

  // 1.检查在重新上小锁的时候当前bucket有没有更新
  acquire(&bcache.lock[hashkey]);
  for (b = bcache.head[hashkey].next; b != &bcache.head[hashkey]; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&bcache.lock[hashkey]);
      release(&bcache.biglock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 2. 在当前位置根据时间戳找个空闲块返回回去

  for (b = bcache.head[hashkey].next; b != &bcache.head[hashkey]; b = b->next)
  {
    if (b->refcnt == 0 && (SelectFreeBlock == 0 || b->timestamp < time))
    {
      SelectFreeBlock = b;
      time = b->timestamp;
    }
  }
  // 找得到就返回
  if (SelectFreeBlock)
  {
    SelectFreeBlock->refcnt++;
    SelectFreeBlock->dev = dev;
    SelectFreeBlock->valid = 0;
    SelectFreeBlock->blockno = blockno;

    release(&bcache.lock[hashkey]);
    release(&bcache.biglock);
    acquiresleep(&SelectFreeBlock->lock);
    return SelectFreeBlock;
  }

  // 3. 从其他bucket中找一个block返回回去

  for (int tmp = hash(hashkey + 1); tmp != hashkey; tmp= hash(tmp + 1))
  {
    acquire(&bcache.lock[tmp]);
    for (b = bcache.head[tmp].next; b != &bcache.head[tmp]; b = b->next)
    {
      if (b->refcnt == 0 && (SelectFreeBlock == 0 || b->timestamp < time))
      {
        SelectFreeBlock = b;
        time = b->timestamp;
      }
    }
    if (SelectFreeBlock)
    {
      SelectFreeBlock->refcnt++;
      SelectFreeBlock->dev = dev;
      SelectFreeBlock->valid = 0;
      SelectFreeBlock->blockno = blockno;

      // 把这个内存块放到指定哈希队列队首
      SelectFreeBlock->next->prev = SelectFreeBlock->prev;
      SelectFreeBlock->prev->next = SelectFreeBlock->next;
      release(&bcache.lock[tmp]);
      SelectFreeBlock->next = bcache.head[hashkey].next;
      SelectFreeBlock->prev = &bcache.head[hashkey];
      bcache.head[hashkey].next->prev = SelectFreeBlock;
      bcache.head[hashkey].next = SelectFreeBlock;

      release(&bcache.lock[hashkey]);
      release(&bcache.biglock);
      acquiresleep(&SelectFreeBlock->lock);
      return SelectFreeBlock;
    }
    release(&bcache.lock[tmp]);
  }
  release(&bcache.lock[hashkey]);
  release(&bcache.biglock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid)
  {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucketkey = hash(b->blockno);

  acquire(&bcache.lock[bucketkey]);
  b->refcnt--;
  if (b->refcnt == 0)
  {
    // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;

    // 由于没有人用，设置最后使用时间
    b->timestamp = ticks;
  }

  release(&bcache.lock[bucketkey]);
}

void bpin(struct buf *b)
{
  int bucketkey = hash(b->blockno);
  acquire(&bcache.lock[bucketkey]);
  b->refcnt++;
  release(&bcache.lock[bucketkey]);
}

void bunpin(struct buf *b)
{
  int bucketkey = hash(b->blockno);
  acquire(&bcache.lock[bucketkey]);
  b->refcnt--;
  release(&bcache.lock[bucketkey]);
}
