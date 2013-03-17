#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>
#include "libc.h"

int accept(int fd, struct sockaddr *restrict addr, socklen_t *restrict len)
{
    int fd2, error;

    error = sys_accept(fd, addr, *len, &fd2);
    if (error) {
        errno = error;
        return -1;
    }

    return fd2;
}
