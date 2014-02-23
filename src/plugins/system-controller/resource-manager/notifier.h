/*
 * Copyright (c) 2013, Intel Corporation
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

#ifndef __MURPHY_SYSTEM_CONTROLLER_NOTIFICATION_H__
#define __MURPHY_SYSTEM_CONTROLLER_NOTIFICATION_H__

#include <sys/types.h>

#include <murphy/resource/data-types.h>

#include "resource-manager.h"

enum mrp_resmgr_event_type_e {
    MRP_RESMGR_EVENT_UNKNOWN = 0,
    MRP_RESMGR_EVENT_ALL     = 0,
    MRP_RESMGR_EVENT_SCREEN,
    MRP_RESMGR_EVENT_AUDIO,
    MRP_RESMGR_EVENT_INPUT,

    MRP_RESMGR_EVENT_MAX
};

enum mrp_resmgr_eventid_e {
    MRP_RESMGR_EVENTID_UNKNOWN      =  0,
    MRP_RESMGR_EVENTID_CREATE,      /* 1 */
    MRP_RESMGR_EVENTID_DESTROY,     /* 2 */
    MRP_RESMGR_EVENTID_INIT,        /* 3 */
    MRP_RESMGR_EVENTID_PREALLOCATE, /* 4 */
    MRP_RESMGR_EVENTID_GRANT,       /* 5 */
    MRP_RESMGR_EVENTID_REVOKE,      /* 6 */
    MRP_RESMGR_EVENTID_COMMIT,      /* 7 */
};

struct mrp_resmgr_notifier_zone_s {
    mrp_list_hook_t events;
    struct {
        size_t screen;
        size_t audio;
        size_t input;
    } nevent;
};

struct mrp_resmgr_notifier_s {
    mrp_resmgr_t *resmgr;
    mrp_resmgr_notifier_zone_t zones[MRP_ZONE_MAX];
    mrp_resmgr_notifier_event_callback_t callback;
};

struct mrp_resmgr_event_s {
    mrp_list_hook_t link;
    mrp_resmgr_event_type_t type;
    mrp_resmgr_eventid_t eventid;
    char *zone;

    /* screen and audio specific fields */
    char *appid;

    union {
        /* screen specific fields */
        struct {
            int32_t surfaceid;
            int32_t layerid;
            char *area;
        };
        /* audio specific fields */
        struct {
            uint32_t audioid;
        };
    };
};



mrp_resmgr_notifier_t *mrp_resmgr_notifier_create(mrp_resmgr_t *resmgr);
void mrp_resmgr_notifier_destroy(mrp_resmgr_notifier_t *notif);

void mrp_resmgr_notifier_register_event_callback(mrp_resmgr_t *resmgr,
                                mrp_resmgr_notifier_event_callback_t callback);

void mrp_resmgr_notifier_queue_screen_event(mrp_resmgr_t *resmgr,
                                            uint32_t zoneid,
                                            const char *zonename,
                                            mrp_resmgr_eventid_t eventid,
                                            const char *appid,
                                            int32_t surfaceid,
                                            int32_t layerid,
                                            const char *area);
void mrp_resmgr_notifier_queue_audio_event(mrp_resmgr_t *resmgr,
                                           uint32_t zoneid,
                                           const char *zonename,
                                           mrp_resmgr_eventid_t eventid,
                                           const char *appid,
                                           uint32_t audioid);

void mrp_notifier_remove_last_event(mrp_resmgr_t *resmgr,
                                    uint32_t zoneid,
                                    mrp_resmgr_event_type_t type);

#define mrp_notifier_remove_last_screen_event(_resmgr, _zoneid)         \
    mrp_notifier_remove_last_event(_resmgr, _zoneid, MRP_RESMGR_EVENT_SCREEN)

#define mrp_notifier_remove_last_audio_event(_resmgr, _zoneid)          \
    mrp_notifier_remove_last_event(_resmgr, _zoneid, MRP_RESMGR_EVENT_AUDIO)

#define mrp_notifier_remove_last_input_event(_resmgr, _zoneid)          \
    mrp_notifier_remove_last_event(_resmgr, _zoneid, MRP_RESMGR_EVENT_INPUT)

void mrp_resmgr_notifier_flush_events(mrp_resmgr_t *resmgr,
                                      uint32_t zoneid,
                                      mrp_resmgr_event_type_t evtype);

#define mrp_resmgr_notifier_flush_screen_events(_resmgr, _zoneid)       \
    mrp_resmgr_notifier_flush_events(_resmgr, _zoneid, MRP_RESMGR_EVENT_SCREEN)

#define mrp_resmgr_notifier_flush_audio_events(_resmgr, _zoneid)        \
    mrp_resmgr_notifier_flush_events(_resmgr, _zoneid, MRP_RESMGR_EVENT_AUDIO)

#define mrp_resmgr_notifier_flush_input_events(_resmgr, _zoneid)        \
    mrp_resmgr_notifier_flush_events(_resmgr, _zoneid, MRP_RESMGR_EVENT_INPUT)

#define mrp_resmgr_notifier_flush_all_events(_resmgr) \
    mrp_resmgr_notifier_flush_events(_resmgr, MRP_RESMGR_EVENT_ALL)

#endif /* __MURPHY_SYSTEM_CONTROLLER_NOTIFICATION_H__ */
