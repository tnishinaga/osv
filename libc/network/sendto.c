#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
    const struct sockaddr *addr, socklen_t alen)
{
    int error;
    ssize_t bytes;

    error = sys_sendto(fd, (caddr_t)buf, len, flags, (caddr_t)addr,
        alen, &bytes);
    if (error) {
        errno = error;
        return -1;
    }

    return bytes;
}
