#include "kernel/types.h"
#include "user/user.h"

signed main(void){
    if(vmprint() < 0) printf("vmprint failed\n");
    exit(0);
}