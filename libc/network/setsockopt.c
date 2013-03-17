#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>

int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    int error;

    error = sys_setsockopt(fd, level, optname, (caddr_t)optval, optlen);
    if (error) {
        errno = error;
        return -1;
    }

    return 0;
}
