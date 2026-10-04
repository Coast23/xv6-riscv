#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    hello();           // 调用我们即将添加的系统调用
    exit(0);
}