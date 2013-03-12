#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <osv/file.h>

/*
 * Global file descriptors table - in OSv we have a single process so file
 * descriptors are maintained globally (not per-thread)
 */

struct file* gfdt[FDMAX] = {0};
volatile unsigned fd_idx = 0;


/* lock-free allocation of a file descriptor */
static int fdalloc(struct file* fp)
{
    int fd;
    do {
        fd = __sync_fetch_and_add(&fd_idx, 1) % FDMAX;
    } while (__sync_val_compare_and_swap(&gfdt[fd], NULL, fp));

    return (FDFIRST + fd);
}

static int fdfree(int fd)
{
    struct file* fp;
    int fdd = fd - FDFIRST;
    if ( (fdd < 0) || (fdd >= FDMAX) ) {
        return -1;
    }

    fp = __sync_lock_test_and_set(&gfdt[fd], NULL);
    if (fp == NULL) {
        return -1;
    }

    return 0;
}

struct file* fget(int fd)
{
    int fdd = fd - FDFIRST;
    if ( (fdd < 0) || (fdd >= FDMAX) ) {
        return NULL;
    }

    return (gfdt[fdd]);
}

int falloc(struct file **resultfp, int *resultfd)
{
    struct file *fp;
    int fd = 0;
    size_t sz = sizeof(struct file);

    fp = malloc(sz);
    if (!fp) {
        return ENOMEM;
    }

    memset(fp, 0, sz);

    fd = fdalloc(fp);
    fp->f_fd = fd;

    /* Start with a refcount of 1 */
    fhold(fp);

    /* Result */
    *resultfp = fp;
    *resultfd = fd;

    return (0);
}

void finit(struct file *fp, unsigned flags, filetype_t type, void *opaque, struct fileops *ops)
{
    fp->f_flags = flags;
    fp->f_type = type;
    fp->f_data = opaque;
    fp->f_ops = ops;
}

int _fdrop(struct file *fp)
{
    int fd = fp->f_fd;
    fo_close(fp);
    fdfree(fd);
    free(fp);

    return (1);
}

int _fnoop(void)
{
    return (0);
}


int
invfo_chmod(struct file *fp, mode_t mode)
{

    return (EINVAL);
}


/*-------------------------------------------------------------------*/

static int
badfo_readwrite(struct file *fp, struct uio *uio, int flags)
{

    return (EBADF);
}

static int
badfo_truncate(struct file *fp, off_t length)
{

    return (EINVAL);
}

static int
badfo_ioctl(struct file *fp, u_long com, void *data)
{

    return (EBADF);
}

static int
badfo_poll(struct file *fp, int events)
{

    return (0);
}

static int
badfo_stat(struct file *fp, struct stat *sb)
{

    return (EBADF);
}

static int
badfo_close(struct file *fp)
{

    return (EBADF);
}

static int
badfo_chmod(struct file *fp, mode_t mode)
{

    return (EBADF);
}

struct fileops badfileops = {
    .fo_read = badfo_readwrite,
    .fo_write = badfo_readwrite,
    .fo_truncate = badfo_truncate,
    .fo_ioctl = badfo_ioctl,
    .fo_poll = badfo_poll,
    .fo_stat = badfo_stat,
    .fo_close = badfo_close,
    .fo_chmod = badfo_chmod,
};
