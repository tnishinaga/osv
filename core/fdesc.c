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



