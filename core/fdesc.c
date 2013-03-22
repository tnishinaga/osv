#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <osv/file.h>
#include <osv/list.h>
#include <osv/poll.h>
#include <osv/debug.h>

/*
 * Global file descriptors table - in OSv we have a single process so file
 * descriptors are maintained globally.
 */
struct file* gfdt[FDMAX] = {0};

/*
 * lock-free allocation of a file descriptor
 */
int fdalloc(struct file* fp)
{
    int fd;
    for (fd=0; fd<FDMAX; fd++) {
        if (gfdt[fd] == NULL) {
            if (__sync_val_compare_and_swap(&gfdt[fd], NULL, fp) == NULL) {
                return fd;
            }
        }
    }

    return EMFILE;
}

/* Try to set a particular fp to another fd */
int fdset(int fd, struct file* fp)
{
    struct file* orig = __sync_val_compare_and_swap(&gfdt[fd], NULL, fp);
    if (orig != NULL) {
        return EBADF;
    }

    return 0;
}

int fdfree(int fd)
{
    struct file* fp;
    if ( (fd < 0) || (fd >= FDMAX) ) {
        return -1;
    }

    fp = __sync_lock_test_and_set(&gfdt[fd], NULL);
    if (fp == NULL) {
        return -1;
    }

    return 0;
}

int fget(int fd, struct file** out_fp)
{
    struct file* fp;
    if ( (fd < 0) || (fd >= FDMAX) ) {
        return EBADF;
    }

    fp = gfdt[fd];
    if (fp == NULL) {
        return EBADF;
    }

    fhold(fp);

    /* FIXME: after the refcount increased, test that it is still allocated
     * atomically, if not, check for a race
     */

    *out_fp = fp;

    return (0);
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
    fp->f_ops = &badfileops;
    list_init(&fp->f_plist);
    mutex_init(&fp->f_lock);

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

    fo_init(fp);
}

void fhold(struct file* fp)
{
    __sync_fetch_and_add((&(fp)->f_count), 1);
}

int fdrop(struct file* fp)
{
    if (__sync_fetch_and_sub((&(fp)->f_count), 1)) {
        return 0;
    }

    /* We are about to free this file structure, but we still do things with it
     * so we increase the refcount by one, fdrop may get called and we don't want
     * to reach this point more than once.
     */
    fhold(fp);

    fo_close(fp);

    poll_drain(fp);
    mutex_destroy(&fp->f_lock);

    free(fp);

    return (1);
}


int
invfo_chmod(struct file *fp, mode_t mode)
{

    return (EINVAL);
}


/*-------------------------------------------------------------------*/

static int
badfo_init(struct file *fp)
{

    return (EBADF);
}

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
    .fo_init = badfo_init,
    .fo_read = badfo_readwrite,
    .fo_write = badfo_readwrite,
    .fo_truncate = badfo_truncate,
    .fo_ioctl = badfo_ioctl,
    .fo_poll = badfo_poll,
    .fo_stat = badfo_stat,
    .fo_close = badfo_close,
    .fo_chmod = badfo_chmod,
};
