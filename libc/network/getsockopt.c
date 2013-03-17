#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>

int getsockopt(int fd, int level, int optname, void *restrict optval, socklen_t *restrict optlen)
{
    int error;
    error = sys_getsockopt(fd, level, optname, optval, optlen);
    if (error) {
        errno = error;
        return -1;
    }

    return 0;
}
