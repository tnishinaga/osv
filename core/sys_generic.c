/*
 * Copyright (c) 2005-2007, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <osv/file.h>
#include <osv/fcntl.h>
#include <osv/vnode.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/*
 * Generic "syscalls" functions wrappers around the fileops abstraction.
 * TODO:
 * Implement sys_poll, sys_chmod
 */

int
sys_close(int fd)
{
    file_t fp;
    int error;

    error = fget(fd, &fp);
    if (error)
        return error;

    /* Release one ref we got by calling fget(); */
    fdrop(fp);

    /* Release one ref count, invokes fo_close() in case there are no holders */
    fdrop(fp);

    return 0;
}

int
sys_read(int fd, struct iovec *iov, size_t niov,
        off_t offset, size_t *count)
{
    struct uio *uio = NULL;
    file_t fp;
    int error;

    error = fget(fd, &fp);
    if (error)
        return error;

    if ((fp->f_flags & FREAD) == 0) {
        fdrop(fp);
        return EBADF;
    }

    error = copyinuio(iov, niov, &uio);
    if (error) {
        fdrop(fp);
        return error;
    }

    if (uio->uio_resid == 0) {
        *count = 0;
        fdrop(fp);
        return 0;
    }

    uio->uio_rw = UIO_READ;
    fo_read(fp, uio, (offset == -1) ? 0 : FOF_OFFSET);
    fdrop(fp);
    *count = uio->uio_resid;

    return error;
}

int
sys_write(int fd, struct iovec *iov, size_t niov,
        off_t offset, size_t *count)
{
    struct uio *uio = NULL;
    file_t fp;
    int ioflags = 0;
    int error;

    error = fget(fd, &fp);
    if (error)
        return error;

    if ((fp->f_flags & FWRITE) == 0) {
        fdrop(fp);
        return EBADF;
    }
    if (fp->f_flags & O_APPEND)
        ioflags |= IO_APPEND;

    error = copyinuio(iov, niov, &uio);
    if (error) {
        fdrop(fp);
        return error;
    }

    if (uio->uio_resid == 0) {
        *count = 0;
        fdrop(fp);
        return 0;
    }

    uio->uio_rw = UIO_WRITE;
    fo_write(fp, uio, ioflags);
    fdrop(fp);

    *count = uio->uio_resid;

    return error;
}


int
sys_ioctl(int fd, u_long request, void *buf)
{
    file_t fp;
    int error;

    error = fget(fd, &fp);
    if (error)
        return error;

    if ((fp->f_flags & (FREAD | FWRITE)) == 0) {
        fdrop(fp);
        return EBADF;
    }

    error = fo_ioctl(fp, request, buf);
    fdrop(fp);

    return error;
}

int
sys_fstat(int fd, struct stat *st)
{
    int error;
    file_t fp;

    error = fget(fd, &fp);
    if (error)
        return error;

    error = fo_stat(fp, st);
    fdrop(fp);

    return error;
}
