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
  // ��ɼ���bucket
  struct spinlock lock[NBUCKET];
  struct buf head[NBUCKET];

  // ����
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

  // ȫ�ŵ���0��
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
  // ���cached�ˣ�ֱ�ӷ���
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
  // �����������ȴ�����ס����С��
  acquire(&bcache.biglock);

  // 1.�����������С����ʱ��ǰbucket��û�и���
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

  // 2. �ڵ�ǰλ�ø���ʱ����Ҹ����п鷵�ػ�ȥ

  for (b = bcache.head[hashkey].next; b != &bcache.head[hashkey]; b = b->next)
  {
    if (b->refcnt == 0 && (SelectFreeBlock == 0 || b->timestamp < time))
    {
      SelectFreeBlock = b;
      time = b->timestamp;
    }
  }
  // �ҵõ��ͷ���
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

  // 3. ������bucket����һ��block���ػ�ȥ

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

      // ������ڴ��ŵ�ָ����ϣ���ж���
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

    // ����û�����ã��������ʹ��ʱ��
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
