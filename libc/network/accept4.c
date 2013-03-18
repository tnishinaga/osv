#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "libc.h"

int accept4(int fd, struct sockaddr *restrict addr, socklen_t *restrict len, int flg)
{
    int fd2, error;

    /* In Release, this flag will be ignored */
    assert(((flg & SOCK_CLOEXEC) == 0));

    error = sys_accept(fd, addr, *len, &fd2);
    if (error) {
        errno = error;
        return -1;
    }

    if (flg & SOCK_NONBLOCK) {
        fcntl(fd, F_SETFL, O_NONBLOCK);
    }

    return fd2;
}
