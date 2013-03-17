#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>

ssize_t recvfrom(int fd, void *restrict buf, size_t len, int flags, struct sockaddr *restrict addr, socklen_t *restrict alen)
{
    int error;
    ssize_t bytes;

    error = sys_recvfrom(fd, (caddr_t)buf, len, flags, addr, alen, &bytes);
    if (error) {
        errno = error;
        return -1;
    }

    return 0;
}
