#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>

int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    int error;

    error = sys_bind(fd, (struct sockaddr *)addr, len);
    if (error) {
        errno = error;
        return -1;
    }

    return 0;
}
