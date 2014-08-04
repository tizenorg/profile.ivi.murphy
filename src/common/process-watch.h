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

#ifndef __MURPHY_PROCESS_WATCH_H__
#define __MURPHY_PROCESS_WATCH_H__

#include <linux/cn_proc.h>

#include <murphy/common/macros.h>
#include <murphy/common/mainloop.h>

MRP_CDECL_BEGIN

/*
 * low-level process watch interface
 *
 * Our low-level process watches essentially map directly to the process
 * event connector interface of the linux kernel. In addition to hiding
 * the details of setting up the netlink socket, subscribing for the events
 * and interpreting the received netlink messages, however it provides a
 * few additional features for convenience. These include finer-grained
 * filtering than offered by the kernel, collecting additional information
 * about a process before delivering events, and discovering already running
 * matching processes and generating pseudoevents for these at the time a
 * client subscribes for events.
 */

typedef struct mrp_proc_watch_s mrp_proc_watch_t;


typedef enum {
    /*
     * raw events (these are available directly from the kernel)
     */
    MRP_PROC_EVENT_NONE     = PROC_EVENT_NONE    , /* 0x00000000 */
    MRP_PROC_EVENT_FORK     = PROC_EVENT_FORK    , /* 0x00000001 */
    MRP_PROC_EVENT_EXEC     = PROC_EVENT_EXEC    , /* 0x00000002 */
    MRP_PROC_EVENT_UID      = PROC_EVENT_UID     , /* 0x00000004 */
    MRP_PROC_EVENT_GID      = PROC_EVENT_GID     , /* 0x00000040 */
    MRP_PROC_EVENT_SID      = PROC_EVENT_SID     , /* 0x00000080 */
    MRP_PROC_EVENT_PTRACE   = PROC_EVENT_PTRACE  , /* 0x00000100 */
    MRP_PROC_EVENT_COMM     = PROC_EVENT_COMM    , /* 0x00000200 */
    MRP_PROC_EVENT_COREDUMP = PROC_EVENT_COREDUMP, /* 0x40000000 */
    MRP_PROC_EVENT_EXIT     = PROC_EVENT_EXIT    , /* 0x80000000 */
    MRP_PROC_EVENT_ALL      =                      /* 0xc00003c7 */
        PROC_EVENT_FORK     | \
        PROC_EVENT_EXEC     | \
        PROC_EVENT_UID      | \
        PROC_EVENT_GID      | \
        PROC_EVENT_SID      | \
        PROC_EVENT_PTRACE   | \
        PROC_EVENT_COMM     | \
        PROC_EVENT_COREDUMP | \
        PROC_EVENT_EXIT,

    /*
     * modifiers for extra functionality
     */

    MRP_PROC_RECURSE        = 0x01000000, /* automatically create watches for
                                           * children */
    MRP_PROC_SCAN_EXISTING  = 0x02000000, /* scan /proc and generate synthetic
                                           * events for existing processes */
    MRP_PROC_IGNORE_THREADS = 0x04000000, /* ignore events for threads */
} mrp_proc_event_type_t;


/*
 * a filter for a process watch
 */

#define MRP_PROC_ANY      NULL
#define MRP_PROC_ANY_PID  0
#define MRP_PROC_ANY_PATH NULL
#define MRP_PROC_ANY_COMM NULL
#define MRP_PROC_ANY_UID  ((uid_t)-1)
#define MRP_PROC_ANY_GID  ((gid_t)-1)

typedef struct {
    pid_t  pid;                          /* pid of interest or 0 */
    char  *path;                         /* exe path of interest, or NULL */
    char  *comm;                         /* process name of interest, or NULL */
    uid_t  uid;                          /* uid of interest, or -1 (eek...) */
    gid_t  gid;                          /* gid of interest, or -1 (eek...) */
} mrp_proc_filter_t;


typedef struct {
    mrp_proc_event_type_t  type;         /* event type */
    struct proc_event     *raw;          /* raw event from the kernel */
} mrp_proc_event_t;


/** Process watch notification callback type. */
typedef void (*mrp_proc_watch_cb_t)(mrp_proc_watch_t *w, mrp_proc_event_t *e,
                                    void *user_data);

/** Add a process watch for the set of events specified by mask. */
mrp_proc_watch_t *mrp_add_proc_watch(mrp_mainloop_t *ml,
                                     mrp_proc_event_type_t mask,
                                     mrp_proc_filter_t *filter,
                                     mrp_proc_watch_cb_t cb, void *user_data);

/** Delete the given process watch. */
void mrp_del_proc_watch(mrp_proc_watch_t *w);


/** Scan /proc and generate synthetic events for existing processes. */
void mrp_scan_proc(mrp_proc_event_type_t mask, mrp_proc_filter_t *filter,
                   mrp_proc_watch_cb_t cb, void *user_data);

/** Increase the reference count of the given process watch. */
mrp_proc_watch_t *mrp_ref_proc_watch(mrp_proc_watch_t *w);

/** Decrease the reference count of the given process watch. */
int mrp_unref_proc_watch(mrp_proc_watch_t *w);


MRP_CDECL_END

#endif /* __MURPHY_PROCESS_WATCH_H__ */
