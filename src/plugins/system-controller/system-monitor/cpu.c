/*
 * Copyright (c) 2012, 2013, Intel Corporation
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

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <murphy/common/debug.h>

#include "cpu.h"


typedef struct {
    uint64_t user;
    uint64_t nice;
    uint64_t system;
    uint64_t idle;
    uint64_t iowait;
    uint64_t irq;
    uint64_t softirq;
    uint64_t steal;
    uint64_t guest;
    uint64_t guest_nice;
} cpu_ticks_t;


typedef struct {
    cpu_ticks_t ticks[2];
    int         curr;
    int         statfd;
} cpu_load_t;



static cpu_load_t load = { .statfd = -1 };


int read_ticks(int fd, cpu_ticks_t *t)
{
    char buf[256], *p, *n;
    int  len;

    lseek(fd, 0, SEEK_SET);

    if ((len = read(fd, buf, sizeof(buf) - 1)) < 0)
        return -1;

    buf[len] = '\0';
    p = buf;

    if (p[0] != 'c' || p[1] != 'p' || p[2] != 'u' || p[3] != ' ') {
        errno = EILSEQ;
        return -1;
    }

    n = NULL;

    p += 4;
    t->user = strtoll(p, &n, 10);

    if (*n != ' ')
        return -1;

    t->nice = strtoll((p = n), &n, 10);

    if (*n != ' ')
        return -1;

    t->system = strtoll((p = n), &n, 10);

    if (*n != ' ')
        return -1;

    t->idle = strtoll((p = n), &n, 10);

    if (*n != ' ')
        return -1;

    t->iowait = strtoll((p = n), &n, 10);

    if (*n != ' ')
        return -1;

    t->irq = strtoll((p = n), &n, 10);

    if (*n != ' ')
        return -1;

    t->softirq = strtoll((p = n), &n, 10);

    if (*n == '\n') {
        t->steal = t->guest = t->guest_nice = 0;
        return 0;
    }
    if (*n != ' ')
        return -1;

    t->steal = strtoll((p = n), &n, 10);

    if (*n == '\n') {
        t->guest = t->guest_nice = 0;
        return 0;
    }
    if (*n != ' ')
        return -1;

    t->guest = strtoll((p = n), &n, 10);

    if (*n == '\n') {
        t->guest_nice = 0;
        return 0;
    }
    if (*n != ' ')
        return -1;

    t->guest_nice = strtoll((p = n), &n, 10);

    if (*n == '\n')
        return 0;

    return -1;
}


static void dump_ticks(char *msg, cpu_ticks_t *t)
{
    mrp_debug("%s: user=%lld, nice=%lld, system=%lld", msg,
              t->user, t->nice, t->system);
    mrp_debug("%s: idle=%lld, iowait=%lld, irq=%lld", msg,
              t->idle, t->iowait, t->irq);
    mrp_debug("%s: softirq=%lld, (%lld, %lld, %lld)", msg,
              t->softirq, t->steal, t->guest, t->guest_nice);
}


int cpu_get_load(int *loadp, int *idlep, int *iowaitp)
{
    cpu_ticks_t *prev, *curr;
    uint64_t     usr, nic, sys, idl, iow, total;

    if (load.statfd < 0) {
        load.statfd = open("/proc/stat", O_RDONLY);

        if (load.statfd < 0)
            return -1;

        if (read_ticks(load.statfd, load.ticks) < 0)
            return -1;

        errno = EAGAIN;
        return -1;
    }

    prev = load.ticks +  !!load.curr;
    curr = load.ticks + !!!load.curr;

    if (read_ticks(load.statfd, curr) < 0)
        return -1;

    dump_ticks("prev: ", prev);
    dump_ticks("curr: ", curr);

#define DIFF(_fld) (curr->_fld - prev->_fld)

    total = (usr = DIFF(user)) + (nic = DIFF(nice)) + (sys = DIFF(system)) +
        (idl = DIFF(idle)) + (iow = DIFF(iowait)) + DIFF(irq) + DIFF(softirq) +
        DIFF(steal) + DIFF(guest) + DIFF(guest_nice);


#undef DIFF

    if (idlep != NULL)
        *idlep = (int)floor(0.5 + 100.0 * idl / total);

    if (iowaitp != NULL)
        *iowaitp = (int)floor(0.5 + 100.0 * iow / total);

    if (loadp != NULL)
        *loadp = (int)(0.5 + 100.0 * (usr + nic + sys + iow) / total);

    load.curr = !load.curr;

    return 0;
}
