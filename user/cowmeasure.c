#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

#define DEFAULT_PAGES 256
#define MAX_PAGES 2048

static void fail(char *s){
    printf("cowmeasure: FAIL: %s\n", s);
    exit(1);
}

static uint64 free_pages(void){
    struct sysinfo info;
    if(sysinfo(&info) < 0) fail("sysinfo");
    return info.free_mem / PGSIZE;
}

static void send_byte(int fd, char c){
    if(write(fd, &c, 1) != 1) fail("write sync");
}

static char recv_byte(int fd){
    char c;
    if(read(fd, &c, 1) != 1) fail("read sync");
    return c;
}

static void sample(char *name, uint64 base){
    uint64 now = free_pages();
    printf("POINT %s %ld %ld\n", name, (long)now, (long)base - (long)now);
}

// Measure free physical pages at five points:
//   A    : after the parent allocates and initializes npages pages
//   B0   : after fork, before the child writes anything
//   B25  : after the child writes 25% of the distinct pages
//   B50  : after the child writes 50% of the distinct pages
//   B100 : after the child writes 100% of the distinct pages
// The child stays alive and blocked between samples, so fork's sharing
// cost and the later copy cost are measured separately.
signed main(int argc, char *argv[]){
    int npages = DEFAULT_PAGES;
    int cmd[2], ack[2];
    char *raw;
    volatile char *pages;
    uint64 a;
    int pid, i, st;

    if(argc > 1){
        npages = atoi(argv[1]);
        // 25% and 50% are computed as npages/4 and npages/2, so keep
        // the page count a multiple of 4 to make the fractions exact.
        if(npages < 4 || npages > MAX_PAGES || (npages % 4) != 0)
            fail("bad page count");
    }

    raw = sbrk(npages * PGSIZE);
    if(raw == SBRK_ERROR) fail("sbrk");
    pages = (volatile char *)raw;
    for(i = 0; i < npages; ++i) pages[i * PGSIZE] = (char)(i + 1);

    // Create the pipes before sampling A, so their kernel pages are not
    // attributed to fork.
    if(pipe(cmd) < 0 || pipe(ack) < 0) fail("pipe");

    a = free_pages();
    printf("cowmeasure: npages=%d\n", npages);
    printf("POINT A %ld 0\n", (long)a);

    pid = fork();
    if(pid < 0) fail("fork");

    if(pid == 0){
        int written = 0;

        send_byte(ack[1], 'r');
        for(;;){
            char c = recv_byte(cmd[0]);
            int target;

            if(c == 'q') exit(0);
            if(c == 'a') target = npages / 4;
            else if(c == 'b') target = npages / 2;
            else if(c == 'c') target = npages;
            else fail("bad command");

            for(; written < target; ++written)
                pages[written * PGSIZE] = (char)(written + 2);
            send_byte(ack[1], 'd');
        }
    }

    if(recv_byte(ack[0]) != 'r') fail("child ready");
    sample("B0", a);

    send_byte(cmd[1], 'a');
    if(recv_byte(ack[0]) != 'd') fail("ack 25");
    sample("B25", a);

    send_byte(cmd[1], 'b');
    if(recv_byte(ack[0]) != 'd') fail("ack 50");
    sample("B50", a);

    send_byte(cmd[1], 'c');
    if(recv_byte(ack[0]) != 'd') fail("ack 100");
    sample("B100", a);

    send_byte(cmd[1], 'q');
    if(wait(&st) != pid || st != 0) fail("child status");
    sample("AFTER", a);

    close(cmd[0]);
    close(cmd[1]);
    close(ack[0]);
    close(ack[1]);
    exit(0);
}
