#include "kernel/types.h"
#include "user/user.h"

static int fixed_go(void){
    int pid1;
    int pid2;
    int status;

    pid1 = fork();
    if(pid1 < 0){
        printf("test_deadlock: first fork failed\n");
        return -1;
    }
    if(pid1 == 0) exit(deadlocktest(0, 1) < 0);

    pid2 = fork();
    if(pid2 < 0){
        printf("test_deadlock: second fork failed\n");
        return -1;
    }
    if(pid2 == 0) exit(deadlocktest(1, 1) < 0);

    if(wait(&status) < 0 || status != 0) return -1;
    if(wait(&status) < 0 || status != 0) return -1;
    
    printf("deadlock fix: PASS\n");
    return 0;
}

static int deadlock_go(void){
    int pid;

    pid = fork();
    if(pid < 0){
        printf("test_deadlock: first fork failed\n");
        return -1;
    }
    if(pid == 0) exit(deadlocktest(0, 0) < 0);

    pid = fork();
    if(pid < 0){
        printf("test_deadlock: second fork failed\n");
        return -1;
    }
    if(pid == 0) exit(deadlocktest(1, 0) < 0);

  //  printf("deadlock mode started; terminate QEMU externally\n");
    return 0;
}

signed main(int argc, char *argv[]){
    int mode;

    if(argc != 2){
        printf("usage: test_deadlock 0|1\n");
        exit(1);
    }
    mode = atoi(argv[1]);
    if(mode == 0) exit(deadlock_go() < 0);
    if(mode == 1) exit(fixed_go() < 0);
    printf("test_deadlock: mode must be 0 or 1\n");
    exit(1);
}
