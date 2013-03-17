#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>
#include "libc.h"

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
    ssize_t bytes;
    int error;

    error = sys_sendmsg(fd, (struct msghdr *)msg, flags, &bytes);
    if (error) {
        errno = error;
        return -1;
    }

    return bytes;
}
