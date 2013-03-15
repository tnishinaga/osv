/*-
 * Copyright (c) 1982, 1986, 1989, 1993
 *  The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <osv/file.h>
#include <osv/poll.h>
#include <osv/list.h>

#include <bsd/porting/synch.h>

int poll_no_poll(int events)
{
    /*
     * Return true for read/write.  If the user asked for something
     * special, return POLLNVAL, so that clients have a way of
     * determining reliably whether or not the extended
     * functionality is present without hard-coding knowledge
     * of specific filesystem implementations.
     */
    if (events & ~POLLSTANDARD)
        return (POLLNVAL);

    return (events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
}

/*
 * Iterate file descriptors and search for pre-existing events,
 * Fill-in the revents for each poll.
 * Returns the number of file descriptors changed
 */
int poll_scan(struct pollfd _pfd[], nfds_t _nfds)
{
    struct file* fp;
    struct pollfd* entry;
    int error, i;
    int nr_events = 0;

    for (i=0; i<_nfds; ++i) {
        entry = &_pfd[i];
        entry->revents = 0;

        error = fget(entry->fd, &fp);
        if (error) {
            entry->revents |= POLLERR;
            nr_events++;
            continue;
        }

        entry->revents = fo_poll(fp, entry->events);
        if (entry->revents) {
            nr_events++;
        }

        fdrop(fp);
    }

    return nr_events;
}

/*
 * Calls wakeup on the poll requests waiting for fd
 */
int poll_wake(int fd)
{
    int error, i;
    list_t head, n, tmp;
    list_t head2, n2, tmp2;

    struct poll_link* pl, *pl2;
    struct pollreq* p;
    struct pollfd* entry_pfd;
    struct file *fp, *fp2;

    error = fget(fd, &fp);
    if (error)
        return EBADF;

    FD_LOCK(fp);
    head = &fp->f_plist;

    if (list_empty(head)) {
        FD_UNLOCK(fp);
        fdrop(fp);
        return EINVAL;
    }

    /*
     * There may be several pollreqs associated with this fd.
     * Wake each and every one.
     *
     * Clears the pollreq references from other fds as well.
     */
    for (n = list_first(head); n != head; n = list_next(n)) {
        pl = list_entry(n, struct poll_link, _link);

        /* p is the current pollreq */
        p = pl->_req;

        /* Remove current pollreq from all file descriptors */
        for (i = 0; i < p->_nfds; ++i) {
            entry_pfd = &p->_pfd[i];
            /* if the current entry is of the signaled fd, don't handle it here.
             */
            if (entry_pfd->fd == fd) {
                continue;
            }

            fget(entry_pfd->fd, &fp2);

            FD_LOCK(fp2);
            if (list_empty(&fp2->f_plist)) {
                FD_UNLOCK(fp2);
                continue;
            }

            /* Search for current pollreq and remove it from list,
             * Also, it may have more than one reference to a single pollreq*/
            head2 = &fp2->f_plist;
            for (n2 = list_first(head2); n2 != head2; n2 = list_next(n2)) {
                pl2 = list_entry(n2, struct poll_link, _link);
                if (pl2->_req == p) {
                    tmp2 = n2->prev;
                    list_remove(n2);
                    free(pl2);
                    n2 = tmp2;
                }
            }

            FD_UNLOCK(fp2);
            fdrop(fp2);
        }

        /* Now, free the poll_link of the signaled fd */
        tmp = n->prev;
        list_remove(n);
        free(pl);
        n = tmp;

        /* Wakeup the poller of this request */
        wakeup((void*)p);
    }

    FD_UNLOCK(fp);
    fdrop(fp);

    return 0;
}

int poll(struct pollfd _pfd[], nfds_t _nfds, int _timeout)
{
    int nr_events, i;
    struct pollfd* entry;
    struct file* fp;
    struct poll_link* pl;
    struct pollreq p = {0};
    size_t pfd_sz = sizeof(struct pollfd) * _nfds;

    if (_nfds > FD_SETSIZE)
        return (EINVAL);

    p._nfds = _nfds;
    p._timeout = _timeout;
    p._pfd = malloc(pfd_sz);
    memcpy(p._pfd, _pfd, pfd_sz);

    /* Any existing events return immediately */
    nr_events = poll_scan(p._pfd, _nfds);
    if (nr_events) {
        memcpy(_pfd, p._pfd, pfd_sz);
        free(p._pfd);
        return 0;
    }

    /* Add pollreq to file descriptor */
    for (i=0; i<_nfds; ++i) {
        entry = &p._pfd[i];
        fget(entry->fd, &fp);

        pl = malloc(sizeof(struct poll_link));
        memset(pl, 0, sizeof(struct poll_link));

        /* Save a reference to request */
        pl->_req = &p;

        FD_LOCK(fp);

        /* Insert at the end */
        list_insert(list_prev(&fp->f_plist), (list_t)pl);

        FD_UNLOCK(fp);
        fdrop(fp);
    }

    /* Block until there's a change with one of the file descriptors */
    msleep((void *)&p, NULL, 0, "poll", p._timeout);

    /* Rescan and return */
    poll_scan(p._pfd, _nfds);
    memcpy(_pfd, p._pfd, pfd_sz);
    free(p._pfd);

    return 0;
}
