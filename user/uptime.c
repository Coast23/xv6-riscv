#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
    printf("uptime: ticks = %d\n", uptime());
    exit(0);
}