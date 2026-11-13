#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

#define NCHILDREN 8
#define LEAK_LOOPS 16
#define LEAK_SLACK 8

static void fail(char *s){
    printf("cowtest: FAIL: %s\n", s);
    exit(1);
}

static char *alloc_pages(int n){
    char *p = sbrk(n * PGSIZE);
    if(p == SBRK_ERROR) fail("sbrk");
    return p;
}

static void make_pipe(int fd[2]){
    if(pipe(fd) < 0) fail("pipe");
}

static void send_byte(int fd, char c){
    if(write(fd, &c, 1) != 1) fail("write sync");
}

static char recv_byte(int fd){
    char c;
    if(read(fd, &c, 1) != 1) fail("read sync");
    return c;
}

static uint64 free_pages(void){
    struct sysinfo info;
    if(sysinfo(&info) < 0) fail("sysinfo");
    return info.free_mem / PGSIZE;
}

// Parent writes a page while it is still shared with the child.
static void test_basic(void){
    int cmd[2], ack[2];
    char *p = alloc_pages(2);
    int pid, st;
    char c;

    p[0] = 'A';
    p[PGSIZE] = 'A';
    make_pipe(cmd);
    make_pipe(ack);

    pid = fork();
    if(pid < 0) fail("basic fork");
    if(pid == 0){
        if(p[0] != 'A' || p[PGSIZE] != 'A') fail("basic child initial");
        c = recv_byte(cmd[0]);
        if(c != 'g') fail("basic sync");
        if(p[PGSIZE] != 'A') fail("basic parent write leaked");
        p[0] = 'C';
        if(p[0] != 'C') fail("basic child write");
        send_byte(ack[1], 'd');
        exit(0);
    }

    // The page is shared, so this store must take the copy path.
    p[PGSIZE] = 'B';
    send_byte(cmd[1], 'g');
    if(recv_byte(ack[0]) != 'd') fail("basic ack");
    if(wait(&st) != pid || st != 0) fail("basic child status");
    if(p[0] != 'A') fail("basic child write leaked");
    if(p[PGSIZE] != 'B') fail("basic parent value");
    close(cmd[0]);
    close(cmd[1]);
    close(ack[0]);
    close(ack[1]);
    printf("cowtest: basic OK\n");
}

// Child writes first; parent must still see its own old contents.
static void test_child_first(void){
    int ack[2], cmd[2];
    char *p = alloc_pages(1);
    int pid, st;
    char c;

    p[0] = 'A';
    make_pipe(ack);
    make_pipe(cmd);

    pid = fork();
    if(pid < 0) fail("child-first fork");
    if(pid == 0){
        if(p[0] != 'A') fail("child-first initial");
        p[0] = 'C';
        send_byte(ack[1], 'd');
        c = recv_byte(cmd[0]);
        if(c != 'g') fail("child-first sync");
        if(p[0] != 'C') fail("child-first own value");
        exit(0);
    }

    if(recv_byte(ack[0]) != 'd') fail("child-first ack");
    if(p[0] != 'A') fail("child-first write leaked");
    p[0] = 'B';
    if(p[0] != 'B') fail("child-first parent write");
    send_byte(cmd[1], 'g');
    if(wait(&st) != pid || st != 0) fail("child-first child status");
    if(p[0] != 'B') fail("child-first parent value changed");
    close(ack[0]);
    close(ack[1]);
    close(cmd[0]);
    close(cmd[1]);
    printf("cowtest: child-first OK\n");
}

// Three processes sharing the same COW pages.
static void test_nested(void){
    char *p = alloc_pages(1);
    int pid, st;

    p[0] = 'A';
    pid = fork();
    if(pid < 0) fail("nested fork");
    if(pid == 0){
        if(p[0] != 'A') fail("nested child initial");
        p[0] = 'B';

        int pid2 = fork();
        if(pid2 < 0) fail("nested grandchild fork");
        if(pid2 == 0){
            if(p[0] != 'B') fail("nested grandchild initial");
            p[0] = 'C';
            if(p[0] != 'C') fail("nested grandchild write");
            exit(0);
        }

        int st2;
        if(wait(&st2) != pid2 || st2 != 0) fail("nested grandchild status");
        if(p[0] != 'B') fail("nested grandchild write leaked");
        exit(0);
    }

    if(wait(&st) != pid || st != 0) fail("nested child status");
    if(p[0] != 'A') fail("nested child write leaked");
    printf("cowtest: nested OK\n");
}

static volatile int readonly_sink;

static void __attribute__((noinline, used)) readonly_target(void){
    readonly_sink = 1;
}

// The text page is R-X-U but not writable and not COW.  A store must
// kill the child; it must not silently create a private copy.
static void test_readonly(void){
    int pid, st;

    pid = fork();
    if(pid < 0) fail("readonly fork");
    if(pid == 0){
        *(volatile char *)readonly_target = 1;
        exit(0);
    }

    if(wait(&st) != pid) fail("readonly wait");
    if(st == 0) fail("read-only text page was writable");
    printf("cowtest: readonly OK (child killed as expected)\n");
}

// copyout() must resolve COW pages for kernel writes: sysinfo() writes
// to a stack page, pipe read() writes to a heap page.
static void test_copyout(void){
    int fd[2];
    char *buf = alloc_pages(1);
    struct sysinfo info;
    int pid, st;

    buf[0] = 'A';
    make_pipe(fd);

    pid = fork();
    if(pid < 0) fail("copyout fork");
    if(pid == 0){
        if(sysinfo(&info) < 0) fail("copyout child sysinfo");
        if(read(fd[0], buf, 4) != 4) fail("copyout pipe read");
        if(memcmp(buf, "test", 4) != 0) fail("copyout pipe data");
        exit(0);
    }

    if(sysinfo(&info) < 0) fail("copyout parent sysinfo");
    if(write(fd[1], "test", 4) != 4) fail("copyout pipe write");
    if(wait(&st) != pid || st != 0) fail("copyout child status");
    if(buf[0] != 'A') fail("copyout child write leaked");
    close(fd[0]);
    close(fd[1]);
    printf("cowtest: copyout OK\n");
}

// Many children share one page; the parent copies while all are alive.
static void test_many(void){
    int ready[2], go[2];
    char *p = alloc_pages(1);
    int i, pid, st;
    char c;

    p[0] = 'A';
    make_pipe(ready);
    make_pipe(go);

    for(i = 0; i < NCHILDREN; i++){
        pid = fork();
        if(pid < 0) fail("many fork");
        if(pid == 0){
            if(p[0] != 'A') fail("many child initial");
            send_byte(ready[1], 'r');
            c = recv_byte(go[0]);
            if(c != 'g') fail("many sync");
            if(p[0] != 'A') fail("many parent write leaked");
            p[0] = 'a' + i;
            if(p[0] != 'a' + i) fail("many child write");
            exit(0);
        }
    }

    for(i = 0; i < NCHILDREN; i++) recv_byte(ready[0]);
    // The page now has NCHILDREN + 1 references.
    p[0] = 'P';
    if(p[0] != 'P') fail("many parent write");
    for(i = 0; i < NCHILDREN; i++) send_byte(go[1], 'g');
    for(i = 0; i < NCHILDREN; i++){
        if(wait(&st) < 0 || st != 0) fail("many child status");
    }
    if(p[0] != 'P') fail("many child write leaked");
    close(ready[0]);
    close(ready[1]);
    close(go[0]);
    close(go[1]);
    printf("cowtest: many OK\n");
}

// exec() frees the child's COW mappings; the parent's pages must survive.
static void test_exec(void){
    char *p = alloc_pages(1);
    int pid, st;

    p[0] = 'A';
    pid = fork();
    if(pid < 0) fail("exec fork");
    if(pid == 0){
        char *argv[] = { "echo", "cow-exec-ok", 0 };
        exec("echo", argv);
        fail("exec");
    }

    if(wait(&st) != pid || st != 0) fail("exec child status");
    if(p[0] != 'A') fail("exec changed parent page");
    printf("cowtest: exec OK\n");
}

// Repeated fork/write/exit must not leak physical pages.
static void test_leak(void){
    char *p = alloc_pages(1);
    uint64 before, after;
    int i, pid, st;

    p[0] = 'A';
    (void)free_pages(); // warm up COW stack pages
    before = free_pages();

    for(i = 0; i < LEAK_LOOPS; i++){
        pid = fork();
        if(pid < 0) fail("leak fork");
        if(pid == 0){
            p[0] = 'x';
            exit(0);
        }
        if(wait(&st) != pid || st != 0) fail("leak child status");
    }

    after = free_pages();
    printf("cowtest: leak check: free pages %ld -> %ld\n", before, after);
    if(before > after && before - after > LEAK_SLACK) fail("page leak");
    printf("cowtest: leak OK\n");
}

signed main(){
    test_basic();
    test_child_first();
    test_nested();
    test_readonly();
    test_copyout();
    test_many();
    test_exec();
    test_leak();
    printf("cowtest: ALL TESTS PASSED\n");
    exit(0);
}