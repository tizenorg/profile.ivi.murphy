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

#ifndef __MURPHY_SYSTEM_MONITOR_MEM_SAMPLER_H__
#define __MURPHY_SYSTEM_MONITOR_MEM_SAMPLER_H__

#include <stdint.h>

/*
 * type of memory usage sampling
 */

typedef enum {
    MEM_SAMPLE_INVALID = -1,             /* invalid type */
    MEM_SAMPLE_MEMFREE,                  /* free memory */
    MEM_SAMPLE_SWAPFREE,                 /* free swap */
    MEM_SAMPLE_DIRTY,                    /* to be written back to disk */
    MEM_SAMPLE_WRITEBACK,                /* actively being written back */
} mem_sample_t;

/*
 * pieces of memory usage information extracted from /proc/meminfo
 */
typedef struct {
    uint64_t mem_total;                  /* MemTotal */
    uint64_t swap_total;                 /* SwapTotal */
    uint64_t mem_free;                   /* MemFree */
    uint64_t swap_free;                  /* SwapFree */
    uint64_t dirty;                      /* Dirty */
    uint64_t writeback;                  /* Writeback */
} mem_usage_t;


/** Sample current memory usage. */
int mem_sample_usage(void);

/** Get memory usage information. */
int mem_get_usage(mem_usage_t *m);

/** Get memory usage of the given type. */
int64_t mem_get_sample(mem_sample_t sample);

/** Get the total amount of memory. */
int64_t mem_get_memory(void);

/** Get the total amount of swap. */
int64_t mem_get_swap(void);

#endif /* __MURPHY_SYSTEM_MONITOR_MEM_SAMPLER_H__ */
