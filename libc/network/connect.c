#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>
#include "libc.h"

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    int error;

    /* FIXME: what to do with len? */
    error = sys_connect(fd, (struct sockaddr *)addr);
    if (error) {
        errno = error;
        return -1;
    }

    return 0;
}
