#ifndef _ZCOPY_H
#define _ZCOPY_H

#ifdef __cplusplus
extern "C" {
#endif

struct zmsghdr {
    struct msghdr zm_msg;
    int zm_fd;
    void *zm_handle;
};

ssize_t zcopy_sendmsg(int sockfd, struct zmsghdr *zm);
void zcopy_close(struct zmsghdr *zm);
ssize_t zcopy_recvmsg(int sockfd, struct zmsghdr *zm);
int zcopy_rxgc(struct zmsghdr *zm);

#ifdef __cplusplus
}
#endif

#endif
