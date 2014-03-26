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
#include <time.h>
#include <math.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>
#include <murphy/common/debug.h>

#include "cpu-sampler.h"
#include "cgroup.h"

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif

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


/*
 * a monitored cgroup
 */

typedef struct {
    mrp_list_hook_t  hook;               /* to list of monitored cgroups */
    int              id;                 /* cgroup 'CPU' id */
    cgroup_t        *cg;                 /* actual cgroup */
    uint64_t        *samples[2];         /* sample buffer */
    uint64_t         tstamps[2];
    int              current;            /* current write index */
} cpu_cgroup_t;


static int           statfd = -1;        /* fd for /proc/stat */
static cpu_t        *cpus;               /* known CPUs */
static int           ncpu;               /* number of CPUs */
static uint32_t      cpu_mask;           /* mask of monitored CPUs */
static cpu_state_t  *states;             /* CPU load/state tracking */
static MRP_LIST_HOOK(cgroups);           /* monitored cgroups */
static int           cgrpid;             /* next cgroup id */


static cpu_cgroup_t *find_cgroup_by_id(int id);


static int enumerate_cpus(void)
{
    cpu_t *cpu;
    char   buf[4096], *p, *e;
    int    i, len;

    if (ncpu > 0)
        return ncpu;

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
    cpu_t           *cpu;
    cpu_cgroup_t    *cgrp;
    mrp_list_hook_t *p, *n;
    int              i;

    if (cpus == NULL && enumerate_cpus() < 0)
        return NULL;

    for (i = 0, cpu = cpus; i < ncpu; i++, cpu++)
        if (cpu->id == id)
            return cpu->name;

    mrp_list_foreach(&cgroups, p, n) {
        cgrp = mrp_list_entry(p, typeof(*cgrp), hook);

        if (cgrp->id <= id && id < cgrp->id + ncpu)
            return cgroup_get_name(cgrp->cg);
    }

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


static int sample_cpus(void)
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


static int sample_cgroup(cpu_cgroup_t *cgrp, uint64_t tstamp)
{
    int rv;

    cgrp->tstamps[cgrp->current] = tstamp;
    rv = cgroup_get_cpu_usage(cgrp->cg, cgrp->samples[cgrp->current], ncpu);
    cgrp->current = !cgrp->current;

    return rv;
}


static int sample_cgroups(void)
{
    mrp_list_hook_t *p, *n;
    cpu_cgroup_t    *cgrp;
    struct timespec  ts;
    uint64_t         tstamp;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    tstamp = ts.tv_sec * 1000 + (ts.tv_nsec + 500) / (1000*1000);

    mrp_list_foreach(&cgroups, p, n) {
        cgrp = mrp_list_entry(p, typeof(*cgrp), hook);

        if (sample_cgroup(cgrp, tstamp) < 0)
            return -1;
    }

    return 0;
}


int cpu_sample_load(void)
{
    if (sample_cpus() < 0 || sample_cgroups() < 0)
        return -1;
    else
        return 0;
}


static int get_cgroup_sample(int cpu, cpu_sample_t sample)
{
    cpu_cgroup_t *cgrp;
    uint64_t      diff, total;
    int           curr, prev, idx, pcnt;

    if ((cgrp = find_cgroup_by_id(cpu)) == NULL)
        return -1;

    if (sample != CPU_SAMPLE_LOAD && sample != CPU_SAMPLE_IDLE)
        return -1;

    idx = (cpu - CPU_SAMPLE_CGROUP) % ncpu;

    curr = !cgrp->current;
    prev = !curr;

    diff  = (cgrp->samples[curr][idx] - cgrp->samples[prev][idx]) / (1000*1000);
    total = cgrp->tstamps[curr] - cgrp->tstamps[prev];
    pcnt  = (int)floor(0.5 + (100.0 * diff) / total);

    /*
     * Notes:
     *     We want to keep our ordinary and cgroup CPU sampling
     *     semantically as identical as possible. When sampling
     *     CPUs we get ticks or jiffies spent in various states
     *     per CPU so calculating the ratio of any sample to the
     *     total will automatically result in the right figure.
     *
     *     However, when sampling cgroup CPU usage, we're getting
     *     CPU usage (in nanoseconds) versus a measurement period.
     *     Now if we have multiple CPUs or cores and we're tracking
     *     the combined virtual CPU, we can get CPU usage up to the
     *     period times the number of CPUs if the cgroup maxes out
     *     all CPUs.
     *
     *     To get to a comparable figure, we might need to scale down
     *     the execution time by the number of physical CPUs/cores.
     */

    if (ncpu > 1 && ((cpu - CPU_SAMPLE_CGROUP) % (ncpu - 1)) == 0)
        pcnt /= (ncpu - 1);

    if (sample == CPU_SAMPLE_LOAD)
        return pcnt;
    else
        return 100 - pcnt;
}


int cpu_get_sample(int cpu, cpu_sample_t sample)
{
    cpu_state_t *state;
    uint64_t     usr, nic, sys, idl, iow, irq, sirq, stl, gst, gstnc, total;

    if (cpus == NULL && enumerate_cpus() < 0)
        return -1;

    if (cpu >= ncpu)
        return get_cgroup_sample(cpu, sample);

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


static cpu_cgroup_t *find_cgroup_by_name(const char *name, cgroup_t **cgp)
{
    mrp_list_hook_t *p, *n;
    cpu_cgroup_t    *cgrp;
    cgroup_t        *cg;

    if (cgp != NULL)
        *cgp = NULL;

    if ((cg = cgroup_open(CGROUP_TYPE_CPUACCT, name, O_RDONLY)) == NULL)
        return NULL;

    mrp_list_foreach(&cgroups, p, n) {
        cgrp = mrp_list_entry(p, typeof(*cgrp), hook);

        /* note that we implicitly pass a reference to the caller here */
        if (cgrp->cg == cg)
            return cgrp;
    }

    if (cgp != NULL)
        *cgp = cg;
    else
        cgroup_unref(cg);

    return NULL;
}


static cpu_cgroup_t *find_cgroup_by_id(int id)
{
    mrp_list_hook_t *p, *n;
    cpu_cgroup_t    *cgrp;

    mrp_list_foreach(&cgroups, p, n) {
        cgrp = mrp_list_entry(p, typeof(*cgrp), hook);

        if (cgrp->id <= id && id < cgrp->id + ncpu)
            return cgrp;
    }

    return NULL;
}


static void unregister_cgroup(cpu_cgroup_t *cgrp)
{
    if (cgrp == NULL)
        return;

    mrp_list_delete(&cgrp->hook);
    mrp_free(cgrp->samples[0]);
    mrp_free(cgrp->samples[1]);
}


static void unref_cgroup(cpu_cgroup_t *cgrp)
{
    if (cgrp == NULL || !cgroup_unref(cgrp->cg))
        return;

    cgrp->cg = NULL;
    unregister_cgroup(cgrp);
}


int cpu_register_cgroup(const char *name)
{
    cpu_cgroup_t *cgrp;
    cgroup_t     *cg;
    int           cpuid;
    const char   *grp, *cpu;
    char          buf[PATH_MAX];

    if (enumerate_cpus() < 0)
        return -1;

    if ((cpu = strchr(name, '#')) == NULL) {
        grp = name;
        cpu = "cpu";
    }
    else {
        int len = cpu - name;

        if (len > (int)sizeof(buf) - 1)
            return -1;

        strncpy(buf, name, len);
        buf[len] = '\0';

        grp = buf;
        cpu++;
    }

    if ((cpuid = cpu_get_id(cpu)) < 0)
        return -1;

    if ((cgrp = find_cgroup_by_name(grp, &cg)) != NULL)
        return cgrp->id + cpuid;
    else if (cg == NULL)
        return -1;

    if ((cgrp = mrp_allocz(sizeof(*cgrp))) == NULL)
        return -1;

    mrp_list_init(&cgrp->hook);
    cgrp->id = CPU_SAMPLE_CGROUP + (ncpu * cgrpid++);
    cgrp->cg = cg;

    cgrp->samples[0] = mrp_allocz(ncpu * sizeof(uint64_t));
    cgrp->samples[1] = mrp_allocz(ncpu * sizeof(uint64_t));

    if (cgrp->samples[0] == NULL || cgrp->samples[1] == NULL)
        goto fail;

    if (cgroup_get_cpu_usage(cgrp->cg, cgrp->samples[0], ncpu) < 0)
        goto fail;

    mrp_list_append(&cgroups, &cgrp->hook);

    return cgrp->id;

 fail:
    if (cgrp != NULL)
        unregister_cgroup(cgrp);
    else
        cgroup_unref(cg);

    return -1;
}


void cpu_unregister_cgroup(int id)
{
    unref_cgroup(find_cgroup_by_id(id));
}
