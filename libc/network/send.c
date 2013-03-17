#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include "libc.h"

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	return sendto(fd, buf, len, flags, 0, 0);
}
