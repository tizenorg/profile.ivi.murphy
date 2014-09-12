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

#ifndef __MURPHY_SYSTEM_MONITOR_H__
#define __MURPHY_SYSTEM_MONITOR_H__

#define SYSMON_MINIMUM_POLLING  1000     /* minimum polling interval */
#define SYSMON_DEFAULT_POLLING 15000     /* default polling interval */

typedef struct sysmon_lua_s        sysmon_lua_t;
typedef struct cpu_watch_lua_s     cpu_watch_lua_t;
typedef struct mem_watch_lua_s     mem_watch_lua_t;
typedef struct process_watch_lua_s process_watch_lua_t;

/** Request deletion of the given CPU watch. */
int sysmon_del_cpu_watch(sysmon_lua_t *sm, cpu_watch_lua_t *w);

/** Request deletion of the given memory watch. */
int sysmon_del_mem_watch(sysmon_lua_t *sm, mem_watch_lua_t *w);

/** Request deletion of the given process watch. */
int sysmon_del_process_watch(sysmon_lua_t *sm, process_watch_lua_t *w);


#endif /* __MURPHY_SYSTEM_MONITOR_H__ */
