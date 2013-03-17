#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>

int socket(int domain, int type, int protocol)
{
    int s, error;

    /* This goes in release... */
    assert(type & SOCK_CLOEXEC == 0);

    error = sys_socket(domain, (type & ~SOCK_NONBLOCK), protocol, &s);
    if (error) {
        errno = error;
        return -1;
    }

    if (type & SOCK_NONBLOCK) {
        fcntl(s, F_SETFD, O_NONBLOCK);
    }

	return s;
}
