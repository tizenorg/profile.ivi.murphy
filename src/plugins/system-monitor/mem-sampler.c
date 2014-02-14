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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <murphy/common/macros.h>
#include <murphy/common/debug.h>

#include "mem-sampler.h"

static mem_usage_t usage;

/*
 * mem_usage_t field descriptor
 */
typedef struct {
    off_t       offs;                    /* offset within mem_usage_t */
    const char *tag;                     /* /proc/meminfo tag */
    size_t      len;                     /* pre-calculated tag length */
} field_t;


static int read_usage(mem_usage_t *m)
{
    /*
     * fields to extract from /proc/meminfo
     *
     * Notes:
     *    Keep the order of the field definition in sync with
     *    the order of tags in /proc/meminfo. For efficiency,
     *    the extracting loop will make at most a single pass
     *    over the tags without ever backtracking. If you have
     *    fields out of order, the extractor will simply fail.
     */
#define FIELD(_fld, _tag)                                               \
 { .offs = MRP_OFFSET(mem_usage_t, _fld), .tag = _tag, .len = sizeof(_tag) - 1 }
    static field_t fields[] = {
        FIELD(mem_total , "MemTotal" ),
        FIELD(mem_free  , "MemFree"  ),
        FIELD(swap_total, "SwapTotal"),
        FIELD(swap_free , "SwapFree" ),
        FIELD(dirty     , "Dirty"    ),
        FIELD(writeback , "Writeback"),
        { (off_t)-1, NULL, 0 }
    };
    static int fd = -1;
#undef FIELD

    field_t    *f;
    uint64_t   *v;
    const char *t, *p;
    char       *n, buf[4096];
    int         l;

    if (fd < 0) {
        if ((fd = open("/proc/meminfo", O_RDONLY)) < 0)
            return -1;

        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }

    lseek(fd, 0, SEEK_SET);

    if ((l = read(fd, buf, sizeof(buf) - 1)) < 0)
        return -1;

    buf[l] = '\0';
    p = buf;

    for (f = fields; f->tag != NULL; f++) {
        v = (uint64_t *)((char *)m + f->offs);
        t = f->tag;
        l = f->len;

        while (p != NULL) {
            if (!strncmp(p, t, l) && p[l] == ':') {
                p  += l + 1;
                *v  = strtoull(p, &n, 10);

                if (*n == ' ' && n[1] == 'k')
                    *v *= 1024;

                if ((n = strchr(n, '\n')) != NULL)
                    p = n + 1;
                else
                    p = NULL;

                goto next_field;
            }
            else {
                if ((n = strchr(p, '\n')) != NULL)
                    p = n + 1;
                else
                    p = NULL;
            }
        }

        if (p == NULL)
            return -1;

    next_field:
        ;
    }

    return 0;
}


static void dump_usage(char *msg, mem_usage_t *m)
{
    mrp_debug("%s: MemTotal=%llu, MemFree=%llu",
              msg, m->mem_total, m->mem_free);
    mrp_debug("%s: SwapTotal=%llu, SwapFree=%llu",
              msg, m->swap_total, m->swap_free);
    mrp_debug("%s: dirty=%llu, writeback=%llu",
              msg, m->dirty, m->writeback);
}


int mem_sample_usage(void)
{
    if (read_usage(&usage) == 0) {
        dump_usage("memory usage", &usage);
        return 0;
    }
    else
        return -1;
}


int mem_get_usage(mem_usage_t *m)
{
    if (read_usage(m) == 0) {
        dump_usage("meminfo", m);
        return 0;
    }
    else
        return -1;
}


int64_t mem_get_sample(mem_sample_t sample)
{
    switch (sample) {
    case MEM_SAMPLE_MEMFREE:   return usage.mem_free;  break;
    case MEM_SAMPLE_SWAPFREE:  return usage.swap_free; break;
    case MEM_SAMPLE_DIRTY:     return usage.dirty;     break;
    case MEM_SAMPLE_WRITEBACK: return usage.writeback; break;
    default:                                           return -1;
    }
}


int64_t mem_get_memory(void)
{
    return usage.mem_total;
}


int64_t mem_get_swap(void)
{
    return usage.swap_total;
}
