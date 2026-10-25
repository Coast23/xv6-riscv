#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "kernel/rwlock.h"
#include "kernel/rwtest.h"

struct rwlock test_lock;
struct spinlock stats_lock;
struct rwtest_stats stats;
int active_readers;
int active_writers;

void rwl_init(struct rwlock *rwl){
    initlock(&rwl->lk, "rwlock");
    rwl->readers = 0;
    rwl->writer_active = 0;
    rwl->waiting_writers = 0;
}

int acquire_read(struct rwlock *rwl){
    int entered = 0; // entered while writer waiting

    acquire(&rwl->lk);
    if(rwl->waiting_writers > 0) entered = 1;
    while(rwl->writer_active || rwl->waiting_writers > 0){
        sleep_prepare(&rwl->readers);
        release(&rwl->lk);
        sleep();
        acquire(&rwl->lk);
        if(rwl->waiting_writers == 0) entered = 0;
    }
    rwl->readers++;
    release(&rwl->lk);
    return entered;
}

void release_read(struct rwlock *rwl){
    acquire(&rwl->lk);
    if(rwl->readers <= 0){
        panic("release_read: no readers to release");
    }
    rwl->readers--;
    if(rwl->readers == 0 && rwl->waiting_writers > 0){
        wakeup(&rwl->writer_active);
    }
    release(&rwl->lk);
}

void acquire_write(struct rwlock *rwl){
    acquire(&rwl->lk);
    rwl->waiting_writers++;

    while(rwl->writer_active || rwl->readers > 0){
        acquire(&stats_lock);
        stats.waiting_writers = 1;
        release(&stats_lock);
        sleep_prepare(&rwl->writer_active);
        release(&rwl->lk);
        sleep();
        acquire(&rwl->lk);
    }
    rwl->waiting_writers--;
    rwl->writer_active = 1;
    acquire(&stats_lock);
    stats.done_writers++;
    release(&stats_lock);
    release(&rwl->lk);
}

void release_write(struct rwlock *rwl){
    acquire(&rwl->lk);
    if(!rwl->writer_active){
        panic("release_write: no writer to release");
    }
    rwl->writer_active = 0;
    if(rwl->waiting_writers > 0){
        wakeup(&rwl->writer_active);
    }
    else{
        wakeup(&rwl->readers);
    }
    release(&rwl->lk);
}

void rwtest_init(void){
    rwl_init(&test_lock);
    initlock(&stats_lock, "rwtest_stats");
    rwtest_reset();
}

int rwtest_wait_writer(void){
    for(;;){
        acquire(&test_lock.lk);
        if(test_lock.waiting_writers > 0){
            release(&test_lock.lk);
            return 0;
        }
        release(&test_lock.lk);
        yield();
        if(killed(myproc())) return -1;
    }
}

int rwtest_worker(int mode, int loops){
    // mode: 0 for read, 1 for write, 2 for late read
    if(mode != 0 && mode != 1 && mode != 2) return -1;
    if(loops < 0) return -1;

    for(int i = 0; i < loops; ++i){
        if(mode == 0 || mode == 2){
            int entered = acquire_read(&test_lock);
            acquire(&stats_lock);
            active_readers++;
            stats.read_ops++;
            if(active_readers > stats.max_readers)
                stats.max_readers = active_readers;
            if(active_writers != 0)
                stats.violations++;
            if(mode == 2 && entered)
                stats.late_readers++;
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
    stats.late_readers = 0;
    stats.waiting_writers = 0;
    stats.done_writers = 0;
    active_readers = 0;
    active_writers = 0;
    release(&stats_lock);
    return 0;
}