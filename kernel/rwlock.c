#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "kernel/rwlock.h"

void rwl_init(struct rwlock *rwl){
    initlock(&rwl->lk, "rwlock");
    rwl->readers = 0;
    rwl->writer_active = 0;
    rwl->waiting_writers = 0;
}

void acquire_read(struct rwlock *rwl){
    acquire(&rwl->lk);
    while(rwl->writer_active){
        sleep_prepare(&rwl->readers);
        release(&rwl->lk);
        sleep();
        acquire(&rwl->lk);
    }
    rwl->readers++;
    release(&rwl->lk);
}

void release_read(struct rwlock *rwl){
    acquire(&rwl->lk);
    if(rwl->readers <= 0){
        panic("release_read: no readers to release");
    }
    rwl->readers--;
    if(rwl->readers == 0){
        wakeup(&rwl->writer_active);
    }
    release(&rwl->lk);
}

void acquire_write(struct rwlock *rwl){
    acquire(&rwl->lk);

    while(rwl->writer_active || rwl->readers > 0){
        sleep_prepare(&rwl->writer_active);
        release(&rwl->lk);
        sleep();
        acquire(&rwl->lk);
    }
    rwl->writer_active = 1;
    release(&rwl->lk);
}

void release_write(struct rwlock *rwl){
    acquire(&rwl->lk);
    if(!rwl->writer_active){
        panic("release_write: no writer to release");
    }
    rwl->writer_active = 0;
    wakeup(&rwl->readers);
    wakeup(&rwl->writer_active);
    release(&rwl->lk);
}