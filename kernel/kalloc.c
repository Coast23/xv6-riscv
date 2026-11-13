// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
extern struct proc proc[NPROC];

// Reference counts for the physical pages managed by this allocator.
// Indexed by (pa - KERNBASE) / PGSIZE, so only the range
// [KERNBASE, PHYSTOP) is covered.  A count of 0 means the page is free
// or not yet handed out; every page returned by kalloc() starts at 1.
#define KREF_NPAGE ((PHYSTOP - KERNBASE) / PGSIZE)
static uint8 krefcnt[KREF_NPAGE];

struct run {
  struct run *next;
};

// Map a physical page address to its krefcnt[] slot.
// Returns -1 if pa is not a page managed by this allocator; the checks
// mirror the ones kfree() has always performed.
static int kref_index(void *pa){
    uint64 p = (uint64)pa;

    if((p % PGSIZE) != 0 || p < (uint64)end || p >= PHYSTOP) return -1;
    return (p - KERNBASE) / PGSIZE;
}

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Drop one reference to the physical page pointed at by pa.
// The page is only returned to the allocator once the last reference
// is gone; shared pages must not be overwritten before that point.
void kfree(void *pa){
    struct run *r;
    int i = kref_index(pa);

    if(i < 0) panic("kfree");

    acquire(&kmem.lock);
    if(krefcnt[i] > 1){
        // Other mappings still use this page.  Just drop this reference.
        krefcnt[i]--;
        release(&kmem.lock);
        return;
    }
    krefcnt[i] = 0;
    release(&kmem.lock);

    // Fill with junk to catch dangling refs.  Only the last reference
    // may scribble on the page.
    memset(pa, 1, PGSIZE);

    r = (struct run *)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void* kalloc(void){
    struct run *r;
    int i;

    acquire(&kmem.lock);
    r = kmem.freelist;
    if(r){
        kmem.freelist = r->next;
        i = kref_index(r);
        if(i < 0) panic("kalloc: bad page on freelist");
        krefcnt[i] = 1;
    }
    release(&kmem.lock);

    if(r) memset((char *)r, 5, PGSIZE); // fill with junk
    return (void *)r;
}

// Add one reference to an already-allocated page.
// Returns 0 on success, -1 if pa is not a valid allocated page.
int kref_inc(void *pa){
    int i = kref_index(pa);

    if(i < 0) return -1;

    acquire(&kmem.lock);
    if(krefcnt[i] == 0 || krefcnt[i] == 255){
        release(&kmem.lock);
        return -1;
    }
    krefcnt[i]++;
    release(&kmem.lock);
    return 0;
}

// Return the current reference count of pa, or -1 if pa is invalid.
int kref_get(void *pa){
    int i = kref_index(pa);
    int n;

    if(i < 0) return -1;

    acquire(&kmem.lock);
    n = krefcnt[i];
    release(&kmem.lock);
    return n;
}

uint64 freeup(void){
    struct run *r;
    uint64 count = 0;

    acquire(&kmem.lock);

    r = kmem.freelist;
    while(r) ++count, r = r->next;

    release(&kmem.lock);

    return count * PGSIZE;
}

uint64 countproc(void){
    struct proc *p;
    uint64 count = 0;

    for(p = proc; p < &proc[NPROC]; ++p){
        acquire(&p->lock);
        if(p->state != UNUSED) ++count;
        release(&p->lock);
    }

    return count;
}