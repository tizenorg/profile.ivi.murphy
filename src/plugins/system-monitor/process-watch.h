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

#ifndef __MURPHY_SYSMON_PROCESS_WATCH_H__
#define __MURPHY_SYSMON_PROCESS_WATCH_H__

#include <murphy/common/macros.h>
#include <murphy/common/list.h>
#include <murphy/common/process-watch.h>
#include <murphy/core/lua-utils/funcbridge.h>

#include "system-monitor.h"

struct process_watch_lua_s {
    mrp_list_hook_t        hook;         /* to list of process watches */
    sysmon_lua_t          *sysmon;       /* system monitor */
    mrp_proc_watch_t      *w;            /* process watch */
    int                    watchref;     /* self-ref system monitor */
    mrp_funcbridge_t      *notify;       /* notification callback */
    mrp_proc_event_type_t  mask;         /* event mask */
    int                    filterref;    /* reference to given filter */
    mrp_proc_filter_t      filter;       /* process filter */
};

/* Create a process watch. */
process_watch_lua_t *process_watch_create(sysmon_lua_t *sm, lua_State *L);

/* Convert an array of integer event names to an event mask integer. */
int process_event_mask(lua_State *L, int idx);

#endif /* __MURPHY_SYSMON_PROCESS_WATCH_H__ */
