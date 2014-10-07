/*
 * Copyright (c) 2014, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "path-track.h"

static void path_changed(mrp_io_watch_t *wd, int fd, mrp_io_event_t events,
        void *user_data)
{
    struct inotify_event *is;
    int bufsize = sizeof(struct inotify_event) + PATH_MAX;
    char buf[bufsize];
    mrp_path_track_t *pt;

    MRP_UNUSED(wd);
    MRP_UNUSED(user_data);

    pt = (mrp_path_track_t *) user_data;

    if (events & MRP_IO_EVENT_IN) {
        int read_bytes;
        int processed_bytes = 0;

        read_bytes = read(fd, buf, bufsize);

        if (read_bytes < 0) {
            mrp_log_error("Failed to read event from inotify: %s",
                    strerror(errno));
            return;
        }

        while (processed_bytes < read_bytes) {
            char *filename = NULL;

            /* the kernel doesn't allow to read incomplete events */
            is = (struct inotify_event *) (buf + processed_bytes);

            processed_bytes += sizeof(struct inotify_event) + is->len;

            if (is->mask & IN_DELETE_SELF) {
                pt->cb(pt, MRP_PATH_INVALID, pt->user_data);
                return;
            }

            if (is->len == 0) {
                /* no file name */
                continue;
            }

            if (strcmp(is->name, pt->file_name) == 0) {
                if (is->mask & IN_CREATE) {
                    pt->cb(pt, MRP_PATH_CREATED, pt->user_data);
                }
                else if (is->mask & IN_DELETE) {
                    pt->cb(pt, MRP_PATH_DELETED, pt->user_data);
                }
            }
        }
    }
}


void mrp_path_track_destroy(mrp_path_track_t *t)
{
    if (!t)
        return;

    if (t->iow) {
        mrp_del_io_watch(t->iow);
    }

    if (t->i_fd && t->s_fd)
        inotify_rm_watch(t->i_fd, t->s_fd);

    close(t->i_fd);

    mrp_free(t->copies[0]);
    mrp_free(t->copies[1]);

    mrp_free(t);
}


mrp_path_track_t * mrp_path_track_create(mrp_mainloop_t *ml,
        const char *path, mrp_path_track_cb cb, void *user_data)
{
    mrp_path_track_t *pt = NULL;

    if (!ml || !path || !cb) {
        mrp_log_error("path-track: forgot a parameter: ml=%p path=%p cb=%p", ml,
                path, cb);
        goto error;
    }

    pt = mrp_allocz(sizeof(*pt));

    if (!pt) {
        mrp_log_error("path-track: allocz");
        goto error;
    }

    pt->cb = cb;
    pt->user_data = user_data;

    pt->copies[0] = mrp_strdup(path);
    pt->copies[1] = mrp_strdup(path);

    if (!pt->copies[0] || !pt->copies[1]) {
        mrp_log_error("path-track: strdup");
        goto error;
    }

    pt->directory_name = dirname(pt->copies[0]);
    pt->file_name = basename(pt->copies[1]);

    pt->i_fd = inotify_init();

    if (pt->i_fd <= 0) {
        mrp_log_error("path-track: inotify_init failed: %s",
                strerror(errno));
        goto error;
    }

    pt->s_fd = inotify_add_watch(pt->i_fd, pt->directory_name,
            IN_CREATE | IN_DELETE | IN_DELETE_SELF);

    if (pt->s_fd < 0) {
        mrp_log_error("path-track: inotify_add_watch (%s) failed: %s",
                pt->directory_name, strerror(errno));
        goto error;
    }

    pt->iow = mrp_add_io_watch(ml, pt->i_fd, MRP_IO_EVENT_IN, path_changed, pt);

    if (!pt->iow) {
        mrp_log_error("path-track: io watch");
        goto error;
    }

    return pt;

error:
    mrp_path_track_destroy(pt);
    return NULL;
}