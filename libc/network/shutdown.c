#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>

int shutdown(int fd, int how)
{
    int error;

    error = sys_shutdown(fd, how);
    if (error) {
        errno = error;
        return -1;
    }

    return 0;
}
