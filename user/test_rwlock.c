#include "kernel/types.h"
#include "kernel/rwtest.h"
#include "user/user.h"

#define READERS 16
#define WRITERS 16
#define READ_LOOPS 100
#define WRITE_LOOPS 200

static int run_workers(int mode, int count, int loops){
    int started = 0;
    for(int i = 0; i < count; ++i){
        int pid = fork();
        if(pid < 0){
            printf("test_rwlock: fork failed\n");
            break;
        }
        if(pid == 0) exit(rwtest_worker(mode, loops) < 0);
        started++;
    }

    for(int i = 0; i < started; ++i){
        if(wait(0) < 0){
            printf("test_rwlock: wait failed\n");
            return -1;
        }
    }
    return started == count ? 0 : -1;
}

static int reader_test(void){
    struct rwtest_stats stats;

    rwtest_reset();
    if(run_workers(0, READERS, READ_LOOPS) < 0) return -1;
    if(rwtest_getstats(&stats) < 0) return -1;

    printf("readers: processes=%d loops=%d max_readers=%d violations=%d\n",
        READERS, READ_LOOPS, stats.max_readers, stats.violations);
    return stats.max_readers >= 2 && stats.violations == 0 ? 0 : -1;
}

static int writer_test(void){
    struct rwtest_stats stats;

    rwtest_reset();
    if(run_workers(1, WRITERS, WRITE_LOOPS) < 0) return -1;
    if(rwtest_getstats(&stats) < 0) return -1;

    printf("writers: processes=%d loops=%d max_writers=%d violations=%d\n",
         WRITERS, WRITE_LOOPS, stats.max_writers, stats.violations);
    return stats.max_writers <= 1 && stats.violations == 0 ? 0 : -1;
}

static int counter_test(void){
    struct rwtest_stats stats;
    int expected = WRITERS * WRITE_LOOPS;

    rwtest_reset();
    if(run_workers(1, WRITERS, WRITE_LOOPS) < 0) return -1;
    if(rwtest_getstats(&stats) < 0) return -1;

    printf("counter: processes=%d loops=%d expected=%d actual=%d\n",
         WRITERS, WRITE_LOOPS, expected, stats.counter);
    return stats.counter == expected && stats.violations == 0 ? 0 : -1;
}

signed main(void){
    int failed = 0;

    if(reader_test() < 0){
        printf("reader test: FAIL\n");
        failed = 1;
    }
    else printf("reader test: PASS\n");

    if(writer_test() < 0){
        printf("writer test: FAIL\n");
        failed = 1;
    }
    else printf("writer test: PASS\n");

    if(counter_test() < 0){
        printf("counter test: FAIL\n");
        failed = 1;
    }
    else printf("counter test: PASS\n");

    exit(failed);
}
