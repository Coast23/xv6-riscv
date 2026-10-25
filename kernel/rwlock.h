#include "kernel/spinlock.h"

struct rwlock {
    struct spinlock lk;
    int readers;
    int writer_active;
    int waiting_writers;
};

void rwl_init(struct rwlock *rwl);
int acquire_read(struct rwlock *rwl);
void release_read(struct rwlock *rwl);
void acquire_write(struct rwlock *rwl);
void release_write(struct rwlock *rwl);