#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"

static struct sleeplock lock_a;
static struct sleeplock lock_b;
static struct spinlock notify_lock;
static int lock_a_held;
static int lock_b_held;
static int initialized;

static void acquire_two(struct sleeplock *a, struct sleeplock *b){
    if(a == b) panic("acquire_two: same lock");
    if(a < b){
        acquiresleep(a);
        acquiresleep(b);
    }
    else{
        acquiresleep(b);
        acquiresleep(a);
    }
}

static void release_two(struct sleeplock *a, struct sleeplock *b){
    if(a == b) panic("release_two: same lock");
    if(a < b){
        releasesleep(b);
        releasesleep(a);
    }
    else{
        releasesleep(a);
        releasesleep(b);
    }
}

static void wait_for_lock(int *flag){
    acquire(&notify_lock);
    while(*flag == 0){
        sleep_prepare(flag);
        release(&notify_lock);
        sleep();
        acquire(&notify_lock);
    }
    release(&notify_lock);
}

static void signal_lock(int *flag){
    acquire(&notify_lock);
    *flag = 1;
    wakeup(flag);
    release(&notify_lock);
}

void deadlocktest_init(void){
    if(initialized) panic("deadlocktest_init: twice");
    initsleeplock(&lock_a, "deadlock-a");
    initsleeplock(&lock_b, "deadlock-b");
    initlock(&notify_lock, "deadlock-notify");
    lock_a_held = 0;
    lock_b_held = 0;
    initialized = 1;
}

int deadlocktest_worker(int role, int mode){
    if(!initialized || (role != 0 && role != 1) || (mode != 0 && mode != 1))
        return -1;

    if(mode == 1){
        acquire_two(&lock_a, &lock_b);
        printk("deadlocktest: role %d acquired both locks\n", role);
        release_two(&lock_a, &lock_b);
        return 0;
    }

    if(role == 0){
        acquiresleep(&lock_a);
        printk("deadlocktest: role 0 acquired A, waiting for B\n");
        signal_lock(&lock_a_held);
        wait_for_lock(&lock_b_held);
        acquiresleep(&lock_b);
    }
    else{
        acquiresleep(&lock_b);
        printk("deadlocktest: role 1 acquired B, waiting for A\n");
        signal_lock(&lock_b_held);
        wait_for_lock(&lock_a_held);
        acquiresleep(&lock_a);
    }
    return -1;
}
