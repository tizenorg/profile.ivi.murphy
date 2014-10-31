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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <murphy/common.h>

enum mrp_path_track_event {
    MRP_PATH_CREATED,
    MRP_PATH_DELETED,
    MRP_PATH_INVALID,
};

typedef struct mrp_path_track_s mrp_path_track_t;

typedef void (*mrp_path_track_cb)(mrp_path_track_t *pt,
        enum mrp_path_track_event e, void *user_data);

struct mrp_path_track_s {
    char *directory_name;
    char *file_name;

    mrp_path_track_cb cb;
    void *user_data;

    int i_fd;
    int s_fd;
    mrp_io_watch_t *iow;

    char* copies[2];

    /* tracking the deletion inside callbacks */
    bool processing_data;
    bool destroyed;
};

mrp_path_track_t * mrp_path_track_create(mrp_mainloop_t *ml,
        const char *path, mrp_path_track_cb cb, void *user_data);

void mrp_path_track_destroy(mrp_path_track_t *t);
