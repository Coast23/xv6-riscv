#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "kernel/rwlock.h"
#include "kernel/rwtest.h"

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

// for testing purposes only
struct rwlock test_lock;
struct spinlock stats_lock;
struct rwtest_stats stats;
int active_readers;
int active_writers;

void rwtest_init(void){
    rwl_init(&test_lock);
    initlock(&stats_lock, "rwtest_stats");
    rwtest_reset();
}

int rwtest_worker(int mode, int loops){
    // mode: 0 for read, 1 for write
    if(mode != 0 && mode != 1) return -1;
    if(loops < 0) return -1;

    for(int i = 0; i < loops; ++i){
        if(mode == 0){
            acquire_read(&test_lock);
            acquire(&stats_lock);
            active_readers++;
            stats.read_ops++;
            if(active_readers > stats.max_readers)
                stats.max_readers = active_readers;
            if(active_writers != 0)
                stats.violations++;
            release(&stats_lock);

            yield();

            acquire(&stats_lock);
            active_readers--;
            release(&stats_lock);
            release_read(&test_lock);
        }
        else{
            acquire_write(&test_lock);
            acquire(&stats_lock);
            active_writers++;
            stats.write_ops++;
            if(active_writers > stats.max_writers)
                stats.max_writers = active_writers;
            if(active_readers != 0 || active_writers > 1)
                stats.violations++;
            stats.counter++;
            release(&stats_lock);

            yield();

            acquire(&stats_lock);
            active_writers--;
            release(&stats_lock);
            release_write(&test_lock);
        }
    }
    return 0;
}

int rwtest_getstats(struct rwtest_stats *out_stats){
    if(out_stats == 0) return -1;
    acquire(&stats_lock);
    *out_stats = stats;
    release(&stats_lock);
    return 0;
}

int rwtest_reset(void){
    acquire(&stats_lock);
    stats.read_ops = 0;
    stats.write_ops = 0;
    stats.counter = 0;
    stats.max_readers = 0;
    stats.max_writers = 0;
    stats.violations = 0;
    active_readers = 0;
    active_writers = 0;
    release(&stats_lock);
    return 0;
}