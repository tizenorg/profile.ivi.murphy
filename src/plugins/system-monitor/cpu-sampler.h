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

#ifndef __MURPHY_SYSTEM_MONITOR_CPU_SAMPLER_H__
#define __MURPHY_SYSTEM_MONITOR_CPU_SAMPLER_H__

/*
 * type of CPU load sampling
 */

typedef enum {
    CPU_SAMPLE_INVALID = -1,             /* invalid type */
    /* these correspond directly to entries in /proc/stat */
    CPU_SAMPLE_USER,                     /* time in user mode */
    CPU_SAMPLE_NICE,                     /*   - || - with low priority */
    CPU_SAMPLE_SYSTEM,                   /* time in system mode */
    CPU_SAMPLE_IDLE,                     /* time in the idle task */
    CPU_SAMPLE_IOWAIT,                   /* time waiting for I/O completion */
    CPU_SAMPLE_IRQ,                      /* time serving interrupts */
    CPU_SAMPLE_SOFTIRQ,                  /* time serving softirqs */
    CPU_SAMPLE_STEAL,                    /* time in other guests */
    CPU_SAMPLE_GUEST,                    /* time running guests */
    CPU_SAMPLE_GUEST_NICE,               /*   -||- with log priority */
    /* these are calculated from the above */
    CPU_SAMPLE_LOAD,                     /* user + nice + system + iowait */
    CPU_SAMPLE_INTERRUPT,                /* irq + softirq */
    CPU_SAMPLE_GUEST_LOAD,               /* guest + guest_nice */
    /* these are not coming from /proc/stat at all */
    CPU_SAMPLE_CGROUP,                   /* time running tasks in a cgroup */
} cpu_sample_t;


/*
 * a CPU name and identifier
 */

typedef struct {
    char *name;                          /* CPU name in /proc/stat */
    int   id;                            /* our identifier for it */
} cpu_t;


/*
 * sampled CPU load
 */

typedef struct {
    int  user;
    int  nice;
    int  system;
    int  idle;
    int  iowait;
    int  irq;
    int  sofirq;
    int  steal;
    int  guest;
    int  guest_nice;
    int  load;
    int  interrupt;
    int  guest_load;
#if 0
    int *cgroup;
#endif
} cpu_load_t;


/** Get the number, names and identifiers of all CPUs in the system. */
int cpu_get_cpus(const cpu_t **cpus);

/** Get the identifier for the given CPU name. */
int cpu_get_id(const char *cpu);

/** Get the CPU name for the given CPU id. */
const char *cpu_get_name(int id);

/** Set the mask of CPU ids to monitor. */
int cpu_set_cpu_mask(uint32_t cpu_mask);

/** Take another sample of the (various) CPU load(s) we track. */
int cpu_sample_load(void);

/** Get the last load for the given CPU and sample type. */
int cpu_get_sample(int cpu, cpu_sample_t sample);

#endif /* __MURPHY_SYSTEM_MONITOR_CPU_SAMPLER_H__ */
