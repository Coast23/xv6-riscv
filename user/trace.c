#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]){
    if(argc < 3){
        fprintf(2, "Usage: trace <mask> <command>\n");
        exit(1);
    }
    int mask = atoi(argv[1]);
    trace(mask);

    if(fork() == 0){
        exec(argv[2], &argv[2]);
        fprintf(2, "exec %s failed\n", argv[2]);
        exit(1);
    }
    wait(0);
    exit(0);
}