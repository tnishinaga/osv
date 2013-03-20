#include <fcntl.h>

#include <sys/stat.h>
#include <osv/file.h>
#include <fs/vfs/vfs.h>

static int vfs_fo_init(struct file *fp)
{
    return 0;
}

static int vfs_close(struct file *fp)
{
    vnode_t vp;
    int error;

    vp = fp->f_vnode;
    vn_lock(vp);
    if ((error = VOP_CLOSE(vp, fp)) != 0) {
        vn_unlock(vp);
        return error;
    }
    vput(vp);
    return 0;
}

static int vfs_read(struct file *fp, struct uio *uio, int flags)
{
    int error;
    size_t count;
    ssize_t bytes;
    struct vnode *vp = fp->f_vnode;

    vn_lock(vp);
    if ((flags & FOF_OFFSET) == 0)
        uio->uio_offset = fp->f_offset;

    bytes = uio->uio_resid;
    error = VOP_READ(vp, uio, 0);
    if (!error) {
        count = bytes - uio->uio_resid;
        uio->uio_resid = count;
        if ((flags & FOF_OFFSET) == 0)
            fp->f_offset += count;
    }

    vn_unlock(vp);
    return error;
}

static int vfs_write(struct file *fp, struct uio *uio, int flags)
{
    int error;
    size_t count;
    ssize_t bytes;
    int ioflags = 0;
    struct vnode *vp = fp->f_vnode;

    if (fp->f_flags & O_APPEND)
        ioflags |= IO_APPEND;

    bytes = uio->uio_resid;

    vn_lock(vp);
    uio->uio_rw = UIO_WRITE;
    if ((flags & FOF_OFFSET) == 0)
        uio->uio_offset = fp->f_offset;

    error = VOP_WRITE(vp, uio, ioflags);
    if (!error) {
        count = bytes - uio->uio_resid;
        uio->uio_resid = count;
        if ((flags & FOF_OFFSET) == 0)
            fp->f_offset += count;
    }
    vn_unlock(vp);

    return error;
}

static int vfs_ioctl(struct file *fp, u_long com, void *data)
{
    int error;
    struct vnode *vp = fp->f_vnode;

    vn_lock(vp);
    error = VOP_IOCTL(vp, fp, com, data);
    vn_unlock(vp);

    return error;
}

static int vfs_stat(struct file *fp, struct stat *st)
{
    int error;
    struct vnode *vp = fp->f_vnode;

    vp = fp->f_vnode;
    vn_lock(vp);
    error = vn_stat(vp, st);
    vn_unlock(vp);

    return error;
}

struct fileops vfs_ops = {
    .fo_init = vfs_fo_init,
    .fo_close = vfs_close,
    .fo_read = vfs_read,
    .fo_write = vfs_write,
    .fo_ioctl = vfs_ioctl,
    .fo_stat = vfs_stat,
    .fo_truncate = NULL,
    .fo_poll = NULL,
    .fo_chmod = NULL,
};

