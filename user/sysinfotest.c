#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]){
    struct sysinfo info;
    if(sysinfo(&info) < 0){
        printf("sysinfo failed\n");
        exit(1);
    }
    printf("freemem = %ld bytes\n", info.free_mem);
    printf("nproc   = %ld\n", info.nproc);
    exit(0);
}