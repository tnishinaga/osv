#ifndef _ZCOPY_H
#define _ZCOPY_H

ssize_t zcopy_sendmsg(int sockfd, struct iovec iov);
ssize_t zcopy_send(int fd, const void *buf, size_t len);
#endif
