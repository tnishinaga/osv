#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>
#include "libc.h"

ssize_t recvmsg(int fd, struct msghdr *msg, int flags)
{
	ssize_t bytes;
	int error;

	error = sys_recvmsg(fd, msg, flags, &bytes);
    if (error) {
        errno = error;
        return -1;
    }

	return bytes;
}
