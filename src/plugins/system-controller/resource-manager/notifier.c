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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <murphy/common.h>

#include "notifier.h"

static void event_destroy(mrp_resmgr_event_t *);

static const char *type_str(mrp_resmgr_event_type_t);
static const char *eventid_str(mrp_resmgr_eventid_t);


mrp_resmgr_notifier_t *mrp_resmgr_notifier_create(mrp_resmgr_t *resmgr)
{
    mrp_resmgr_notifier_t *notifier;
    mrp_resmgr_notifier_zone_t *nz;
    int i;

    MRP_ASSERT(resmgr, "invalid argument");

    if ((notifier = mrp_allocz(sizeof(mrp_resmgr_notifier_t)))) {
        notifier->resmgr = resmgr;

        for (i = 0;  i < MRP_ZONE_MAX;  i++) {
            nz = notifier->zones + i;
            mrp_list_init(&nz->events);
        }
    }

    return notifier;
}

void mrp_resmgr_notifier_destroy(mrp_resmgr_notifier_t *notifier)
{
    mrp_list_hook_t *events, *entry, *n;
    mrp_resmgr_notifier_zone_t *nz;
    mrp_resmgr_event_t *ev;
    int i;

    if (notifier) {

        for (i = 0;  i < MRP_ZONE_MAX;  i++) {
            nz = notifier->zones + i;
            events = &nz->events;

            mrp_list_foreach(events, entry, n) {
                ev = mrp_list_entry(entry, mrp_resmgr_event_t, link);
                event_destroy(ev);
            }
        }

        mrp_free((void *)notifier);
    }
}

void mrp_resmgr_notifier_register_event_callback(mrp_resmgr_t *resmgr,
                                mrp_resmgr_notifier_event_callback_t callback)
{
    mrp_resmgr_notifier_t *notifier;

    MRP_ASSERT(resmgr && resmgr->notifier, "invalid argument");

    notifier = resmgr->notifier;
    notifier->callback = callback;
}

void mrp_resmgr_notifier_queue_screen_event(mrp_resmgr_t *resmgr,
                                            uint32_t zoneid,
                                            mrp_resmgr_eventid_t eventid,
                                            const char *appid,
                                            int32_t surfaceid,
                                            int32_t layerid,
                                            const char *area)
{
    mrp_resmgr_notifier_t *notifier;
    mrp_resmgr_notifier_zone_t *nz;
    mrp_resmgr_event_t *ev;

    MRP_ASSERT(resmgr && resmgr->notifier && zoneid < MRP_ZONE_MAX && appid,
               "invalid argument");

    notifier = resmgr->notifier;
    nz = notifier->zones + zoneid;

    if ((ev = mrp_allocz(sizeof(mrp_resmgr_event_t)))) {

        ev->type = MRP_RESMGR_EVENT_SCREEN;
        ev->eventid = eventid;
        ev->appid = mrp_strdup(appid);
        ev->surfaceid = surfaceid;
        ev->layerid = layerid;
        ev->area = mrp_strdup(area ? area : "<unknown>");

        mrp_list_append(&nz->events, &ev->link);

        mrp_debug("queued screen event in zone %u (eventid=%s appid='%s' "
                  "surfaceid=%d layerid=%d, area='%s')",
                  zoneid, eventid_str(ev->eventid), ev->appid,
                  ev->surfaceid, ev->layerid, ev->area);

        nz->nevent.screen++;
    }
}

void mrp_resmgr_notifier_queue_audio_event(mrp_resmgr_t *resmgr,
                                           uint32_t zoneid,
                                           mrp_resmgr_eventid_t eventid,
                                           const char *appid,
                                           uint32_t audioid,
                                           const char *zonename)
{
    mrp_resmgr_notifier_t *notifier;
    mrp_resmgr_notifier_zone_t *nz;
    mrp_resmgr_event_t *ev;

    MRP_ASSERT(resmgr && resmgr->notifier && zoneid < MRP_ZONE_MAX && appid,
               "invalid argument");

    notifier = resmgr->notifier;
    nz = notifier->zones + zoneid;

    if ((ev = mrp_allocz(sizeof(mrp_resmgr_event_t)))) {

        ev->type = MRP_RESMGR_EVENT_AUDIO;
        ev->eventid = eventid;
        ev->appid = mrp_strdup(appid);
        ev->audioid = audioid;
        ev->zone = mrp_strdup(zonename ? zonename : "<unknown>");

        mrp_list_append(&nz->events, &ev->link);

        mrp_debug("queued audio event in zone %u (eventid=%s appid='%s')",
                  zoneid, eventid_str(ev->eventid), ev->appid);

        nz->nevent.audio++;
    }
}

void mrp_notifier_remove_last_event(mrp_resmgr_t *resmgr,
                                    uint32_t zoneid,
                                    mrp_resmgr_event_type_t type)
{
    mrp_resmgr_notifier_t *notifier;
    mrp_resmgr_notifier_zone_t *nz;
    mrp_list_hook_t *events, *entry, *n;
    mrp_resmgr_event_t *ev;
    size_t *nevent_ptr;
    int it;

    MRP_ASSERT(resmgr && resmgr->notifier && zoneid < MRP_ZONE_MAX,
               "invalid argument");

    notifier = resmgr->notifier;
    nz = notifier->zones + zoneid;

    switch (type) {

    case MRP_RESMGR_EVENT_SCREEN:   nevent_ptr = &nz->nevent.screen;   break;
    case MRP_RESMGR_EVENT_AUDIO:    nevent_ptr = &nz->nevent.audio;    break;
    case MRP_RESMGR_EVENT_INPUT:    nevent_ptr = &nz->nevent.input;    break;

    default:
        mrp_log_error("system-controller: failed to remove last resource "
                      "manager event: invalid event type %d", type);
        return;
    }

    it = 0;
    events = &nz->events;

    mrp_list_foreach(events, entry, n) {
        ev = mrp_list_entry(entry, mrp_resmgr_event_t, link);

        if (++it > MRP_RESMGR_EVENT_MAX)
            break;
        
        if (type == ev->type) {
            mrp_debug("removing %s event in zone %u (eventid=%s "
                      "appid='%s' surfaceid=%d layerid=%d, area='%s'",
                      type_str(ev->type), zoneid, eventid_str(ev->eventid),
                      ev->appid, ev->surfaceid, ev->layerid, ev->area);

            event_destroy(ev);

            (*nevent_ptr)--;

            return;
        }
    }

    mrp_log_error("system-controller: failed to remove last %s event: "
                  "non-existent event", type_str(type));
}

void mrp_resmgr_notifier_flush_events(mrp_resmgr_t *resmgr,
                                      uint32_t zoneid,
                                      mrp_resmgr_event_type_t type)
{
    mrp_resmgr_notifier_t *notifier;
    mrp_resmgr_notifier_zone_t *nz;
    mrp_list_hook_t *events, *entry, *n;
    mrp_resmgr_event_t *ev;
    size_t nevent, *nevent_ptr;
    const char *str1, *str2;

    MRP_ASSERT(resmgr && resmgr->notifier && zoneid < MRP_ZONE_MAX,
               "invalid argument");

    notifier = resmgr->notifier;
    nz = notifier->zones + zoneid;
    str1 = str2 = "";

    switch (type) {

    case MRP_RESMGR_EVENT_ALL:
        str1 = "all ";
        nevent = nz->nevent.screen + nz->nevent.audio  + nz->nevent.input;
        nevent_ptr = NULL;
        break;

    case MRP_RESMGR_EVENT_SCREEN:
        str2 = " screen";
        nevent = nz->nevent.screen;
        nevent_ptr = &nz->nevent.screen;
        break;

    case MRP_RESMGR_EVENT_AUDIO:
        str2 = " audio";
        nevent = nz->nevent.audio;
        nevent_ptr = &nz->nevent.audio;
        break;

    case MRP_RESMGR_EVENT_INPUT:
        str2 = " input";
        nevent = nz->nevent.input;
        nevent_ptr = &nz->nevent.input;
        break;

    default:
        mrp_log_error("system-controller: failed to flush resource manager "
                      "events: invalid event type %d", type);
        return;
    }

    if (!nevent) {
        mrp_debug("%s event queue in zone %u is empty: nothing to flush",
                  type_str(type), zoneid);
        return;
    }

    mrp_debug("%s %s%d%s events in zone %u",
              notifier->callback ? "forwarding" : "throwing away",
              str1, nevent, str2, zoneid);

    events = &nz->events;

    mrp_list_foreach(events, entry, n) {
        ev = mrp_list_entry(entry, mrp_resmgr_event_t, link);

        if (type == MRP_RESMGR_EVENT_ALL || type == ev->type) {
            if (notifier->callback)
                notifier->callback(resmgr, ev);

            event_destroy(ev);

            if (nevent_ptr)
                (*nevent_ptr)--;
        }
    }

    if (!nevent_ptr) {
        MRP_ASSERT(mrp_list_empty(&nz->events),
                   "confused with data structures");
        memset(&nz->nevent, 0, sizeof(nz->nevent));
    }
}

static void event_destroy(mrp_resmgr_event_t *ev)
{
    if (ev) {
        mrp_list_delete(&ev->link);

        mrp_free((void *)ev->appid);

        switch (ev->type) {

        case MRP_RESMGR_EVENT_SCREEN:
            mrp_free((void *)ev->area);
            break;

        case MRP_RESMGR_EVENT_AUDIO:
            mrp_free((void *)ev->zone);
            break;

        case MRP_RESMGR_EVENT_INPUT:
            break;

        default:
            mrp_log_error("system-controller: confused with data structures: "
                          "invalid resource manager event type %d", ev->type);
            break;
        }

        mrp_free(ev);
    }
}


static const char *type_str(mrp_resmgr_event_type_t type)
{
    switch (type) {
    case MRP_RESMGR_EVENT_ALL:     return "all";
    case MRP_RESMGR_EVENT_SCREEN:  return "screen";
    case MRP_RESMGR_EVENT_AUDIO:   return "audio";
    case MRP_RESMGR_EVENT_INPUT:   return "input";
    default:                       return "<unknown>";
    }
}

static const char *eventid_str(mrp_resmgr_eventid_t eventid)
{
    switch (eventid) {
    case MRP_RESMGR_EVENTID_GRANT:    return "grant";
    case MRP_RESMGR_EVENTID_REVOKE:   return "revoke";
    default:                          return "<unknown>";
    }
}
