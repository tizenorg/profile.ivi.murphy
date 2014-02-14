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

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>
#include <murphy/common/debug.h>

#include "cpu-sampler.h"

/*
 * CPU 'ticks' (entry for a single CPU in /proc/stat)
 */

typedef struct {                         /* time spent in various states */
    uint64_t user;                       /* in user mode */
    uint64_t nice;                       /*     - || - with low priority */
    uint64_t system;                     /* in system mode */
    uint64_t idle;                       /* in the idle task */
    uint64_t iowait;                     /* waiting for I/O completion */
    uint64_t irq;                        /* servicing interrupts */
    uint64_t softirq;                    /* servicing softirqs */
    uint64_t steal;                      /* in other guests (if virtualized) */
    uint64_t guest;                      /* running guests */
    uint64_t guest_nice;                 /*     - || - with low priority */
} ticks_t;


/*
 * calculated CPU load
 */

typedef struct {
    ticks_t samples[2];                  /* tick sample buffer */
    int     current;                     /* current write index */
} cpu_state_t;


static int       statfd = -1;            /* fd for /proc/stat */
static cpu_t    *cpus;                   /* known CPUs */
static int       ncpu;                   /* number of CPUs */
static uint32_t  cpu_mask;               /* mask of monitored CPUs */
static cpu_state_t *states;              /* CPU load/state tracking */


static int enumerate_cpus(void)
{
    cpu_t *cpu;
    char   buf[4096], *p, *e;
    int    i, len;

    if (statfd < 0) {
        if ((statfd = open("/proc/stat", O_RDONLY)) < 0)
            return -1;

        fcntl(statfd, F_SETFD, FD_CLOEXEC);
    }

    if ((len = read(statfd, buf, sizeof(buf) - 1)) <= 0)
        return -1;
    else
        buf[len] = '\0';

    p = buf;
    while (p[0] == 'c' && p[1] == 'p' && p[2] == 'u') {
        for (e = p; *e != ' ' && *e; e++)
            ;

        if (!*e) {
            errno = EILSEQ;
            goto fail;
        }

        if (mrp_reallocz(cpus, ncpu, ncpu + 1) == NULL)
            goto fail;
        else
            cpu = cpus + ncpu;

        if ((cpu->name = mrp_datadup(p, e - p + 1)) == NULL)
            goto fail;

        cpu->name[e-p] = '\0';
        cpu->id        = ncpu++;

        mrp_debug("CPU #%d is '%s'", cpu->id, cpu->name);

        if ((p = strchr(p, '\n')) != NULL)
            p++;
        else
            break;
    }

    if ((states = mrp_allocz(ncpu * sizeof(*states))) == NULL)
        goto fail;

    for (i = 0; i < ncpu; i++)
        states[i].current = -1;

    cpu_set_cpu_mask(-1);

    return ncpu;

 fail:
    if (cpus != NULL) {
        for (i = 0, cpu = cpus; i < ncpu; cpu++, i++)
            mrp_free(cpu->name);

        mrp_free(cpus);
        cpus = NULL;
        ncpu = 0;
    }

    return -1;
}


int cpu_get_id(const char *name)
{
    cpu_t *cpu;
    int    i;

    if (cpus == NULL && enumerate_cpus() < 0)
        return -1;

    for (i = 0, cpu = cpus; i < ncpu; i++, cpu++)
        if (!strcmp(cpu->name, name))
            return cpu->id;

    return -1;
}


const char *cpu_get_name(int id)
{
    cpu_t *cpu;
    int    i;

    if (cpus == NULL && enumerate_cpus() < 0)
        return NULL;

    for (i = 0, cpu = cpus; i < ncpu; i++, cpu++)
        if (cpu->id == id)
            return cpu->name;

    return NULL;
}


int cpu_get_cpus(const cpu_t **cpusptr)
{
    if (cpus == NULL && enumerate_cpus() < 0)
        return -1;

    if (cpusptr != NULL)
        *cpusptr = cpus;

    return ncpu;
}


int cpu_set_cpu_mask(uint32_t mask)
{
    if (cpus == NULL && enumerate_cpus() < 0)
        return -1;

    cpu_mask = (mask & ((1 << ncpu) - 1));

    return 0;
}


static int sample_load(void)
{
    char      buf[4096], *p;
    int       len;
    uint32_t  cmask, cid;
    ticks_t  *t;

    lseek(statfd, 0, SEEK_SET);

    if ((len = read(statfd, buf, sizeof(buf) - 1)) < 0)
        return -1;

    buf[len] = '\0';
    p = buf;

    cmask = cpu_mask;
    cid   = 0;

    while (cmask) {
        if (!(cmask & (1 << cid)))
            goto next_cpu;

        if (p[0] != 'c' || p[1] != 'p' || p[2] != 'u')
            goto parse_error;

        p += 3;
        while ('0' <= *p && *p <= '9')
            p++;

        if (*p != ' ')
            goto parse_error;
        else
            p++;

        t = states[cid].samples + (states[cid].current & 0x1 ? 0 : 1);

#define PARSE_FIELD(_f, _next) do {                             \
            t->_f = strtoll(p, &p, 10);                         \
                                                                \
            if (*p != _next)                                    \
                goto parse_error;                               \
        } while (0)

        PARSE_FIELD(user      , ' ' );
        PARSE_FIELD(nice      , ' ' );
        PARSE_FIELD(system    , ' ' );
        PARSE_FIELD(idle      , ' ' );
        PARSE_FIELD(iowait    , ' ' );
        PARSE_FIELD(irq       , ' ' );
        PARSE_FIELD(softirq   , ' ' );
        PARSE_FIELD(steal     , ' ' );
        PARSE_FIELD(guest     , ' ' );
        PARSE_FIELD(guest_nice, '\n');

#undef PARSE_FIELD

        cmask &= ~(1 << cid);

        if (MRP_UNLIKELY(states[cid].current == -1)) {
            states[cid].samples[1] = states[cid].samples[0];
            states[cid].current = 0;
        }
        else
            states[cid].current = !states[cid].current;

    next_cpu:
        cid++;
        if (*p == '\n' || (p = strchr(p, '\n')) != NULL)
            p++;
        else {
            if (cmask != 0) {
                errno = ENODATA;
                return -1;
            }
        }
    }

    return 0;

 parse_error:
    errno = EILSEQ;
    return -1;
}


int cpu_sample_load(void)
{
    return sample_load();
}


int cpu_get_sample(int cpu, cpu_sample_t sample)
{
    cpu_state_t *state;
    uint64_t     usr, nic, sys, idl, iow, irq, sirq, stl, gst, gstnc, total;

    if (cpus == NULL && enumerate_cpus() < 0)
        return -1;

    if (cpu >= ncpu || !(cpu_mask & (1 << cpu))) {
        errno = ENOENT;
        return -1;
    }

    if (sample < 0 || sample > CPU_SAMPLE_GUEST_LOAD) {
        errno = ENOENT;
        return -1;
    }

    state = states + cpu;

#define DIFF(_fld)                              \
    state->samples[ state->current]._fld -      \
        state->samples[!state->current]._fld
    total  = (usr   = DIFF(user));
    total += (nic   = DIFF(nice));
    total += (sys   = DIFF(system));
    total += (idl   = DIFF(idle));
    total += (iow   = DIFF(iowait));
    total += (irq   = DIFF(irq));
    total += (sirq  = DIFF(softirq));
    total += (stl   = DIFF(steal));
    total += (gst   = DIFF(guest));
    total += (gstnc = DIFF(guest_nice));
#undef DIFF

    if (total == 0) {
        errno = EAGAIN;
        return -1;
    }

    switch (sample) {
#define PCNT(v) ((int)floor(0.5 + 100.0 * (v) / total))
    case CPU_SAMPLE_USER:       return PCNT(usr);
    case CPU_SAMPLE_NICE:       return PCNT(nic);
    case CPU_SAMPLE_SYSTEM:     return PCNT(sys);
    case CPU_SAMPLE_IDLE:       return PCNT(idl);
    case CPU_SAMPLE_IOWAIT:     return PCNT(iow);
    case CPU_SAMPLE_IRQ:        return PCNT(irq);
    case CPU_SAMPLE_SOFTIRQ:    return PCNT(sirq);
    case CPU_SAMPLE_STEAL:      return PCNT(stl);
    case CPU_SAMPLE_GUEST:      return PCNT(gst);
    case CPU_SAMPLE_GUEST_NICE: return PCNT(gstnc);
    case CPU_SAMPLE_LOAD:       return PCNT(usr + nic + sys + iow);
    case CPU_SAMPLE_INTERRUPT:  return PCNT(irq + sirq);
    case CPU_SAMPLE_GUEST_LOAD: return PCNT(gst + gstnc);
    default:                    return -1;
#undef PCNT
    }
}
