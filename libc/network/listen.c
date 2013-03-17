#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>

int listen(int fd, int backlog)
{
    int error;

    error = sys_listen(fd, backlog);
    if (error) {
        errno = error;
        return -1;
    }

    return 0;
}
