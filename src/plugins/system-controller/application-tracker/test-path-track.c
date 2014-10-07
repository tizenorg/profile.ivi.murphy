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

static void cb(mrp_path_track_t *pt, enum mrp_path_track_event e,
        void *user_data)
{
    mrp_mainloop_t *ml = (mrp_mainloop_t *) user_data;

    printf("cb: %s/%s", pt->directory_name, pt->file_name);

    switch (e) {
        case MRP_PATH_CREATED:
            printf(" created\n");
            break;
        case MRP_PATH_DELETED:
            printf(" deleted\n");
            break;
        case MRP_PATH_INVALID:
            printf(" is now invalid\n");
            mrp_mainloop_quit(ml, 0);
            break;
    }

    return;
}

int main(int argc, char *argv[]) {

    mrp_mainloop_t *ml = mrp_mainloop_create();
    void *user_data = ml;
    char *path = "/tmp/foo";
    mrp_path_track_t *pts[argc];
    int i;

    for (i = 1; i < argc; i++) {
        printf("argv[%i]: %s\n", i, argv[i]);
        pts[i] = mrp_path_track_create(ml, argv[i], cb, user_data);

        if (pts[i] == NULL) {
            printf("failed to add argument '%s'\n", argv[i]);
            exit(1);
        }
    }


    mrp_mainloop_run(ml);

    for (i = 1; i < argc; i++) {
        mrp_path_track_destroy(pts[i]);
    }

    mrp_mainloop_destroy(ml);
}

/*  gcc `pkg-config --libs --cflags murphy-common` test-path-track.c path-track.c -o test-path-track */