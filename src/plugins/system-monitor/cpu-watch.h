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

#ifndef __MURPHY_CPU_WATCH_H__
#define __MURPHY_CPU_WATCH_H__

#include <murphy/common/macros.h>
#include <murphy/common/list.h>
#include <murphy/core/lua-utils/funcbridge.h>

#include "system-monitor.h"
#include "cpu-sampler.h"
#include "estimator.h"

#define CPU_WATCH_WINDOW (60 * 1000)     /* default estimating window */

/*
 * a CPU watch
 */

typedef struct {
    char *label;                         /* label to use in notifications */
    int   limit;                         /* limit value in percentages */
} cpu_limit_t;


struct cpu_watch_lua_s {
    mrp_list_hook_t   hook;              /* to list of CPU watches */
    sysmon_lua_t     *sysmon;            /* system monitor */
    int               cpu;               /* CPU to watch */
    cpu_sample_t      sample;            /* sample to watch (what to measure) */
    cpu_limit_t      *limits;            /* limits to notify about */
    int               limref;            /* Lua reference to limit table */
    int               polling;           /* polling interval */
    int               window;            /* window to estimate over (msecs) */
    ewma_t            value;             /* estimated value */
    cpu_limit_t      *curr;              /* currently active limit */
    cpu_limit_t      *prev;              /* previously active limit */
    int               watchref;          /* self-ref system monitor */
    mrp_funcbridge_t *notify;            /* notification callback */
    mrp_funcbridge_t *update;            /* overridable update method */
};

/** Create a CPU watch. */
cpu_watch_lua_t *cpu_watch_create(sysmon_lua_t *sm, int polling, lua_State *L);

/** Let the CPU watch know the polling interval (in milliseconds). */
void cpu_watch_set_polling(cpu_watch_lua_t *w, int polling);

/** Set the CPU watch estimate window (in milliseconds). */
void cpu_watch_set_window(cpu_watch_lua_t *w, int window);

/** Update the CPU watch with the latest sampled value. */
int cpu_watch_update(cpu_watch_lua_t *w, lua_State *L);

/** Invoke the CPU watch state/limit change notification callback. */
void cpu_watch_notify(cpu_watch_lua_t *w, lua_State *L);

#endif /* __MURPHY_CPU_WATCH_H__ */
