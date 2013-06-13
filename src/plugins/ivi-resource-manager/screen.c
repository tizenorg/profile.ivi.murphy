/*
 * Copyright (c) 2012, Intel Corporation
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

#include <murphy/common.h>

#include <murphy/resource/config-api.h>
#include <murphy/resource/manager-api.h>

#include "screen.h"
#include "class.h"

#define ATTRIBUTE(n,t,v)    {n, MRP_RESOURCE_RW, mqi_##t, {.t=v}}
#define ATTR_END            {NULL, 0, 0, {.string=NULL}}

typedef struct screen_resource_s   screen_resource_t;

struct mrp_resmgr_screen_s {
    mrp_resmgr_data_t *data;
    uint32_t resid;
    mrp_list_hook_t classes[MRP_ZONE_MAX];
};

struct screen_resource_s {
    mrp_list_hook_t link;
    mrp_resource_t *res;
    mrp_resmgr_screen_t *screen;
    mrp_resmgr_class_t *class;
    bool active;
    uint32_t key;
};


static screen_resource_t *screen_resource_create(mrp_resmgr_screen_t *,
                                                 mrp_zone_t *,mrp_resource_t *,
                                                 mrp_application_class_t *);
static void screen_resource_destroy(mrp_resmgr_screen_t *,  mrp_resource_t *);
static screen_resource_t *screen_resource_lookup(mrp_resmgr_screen_t *,
                                                 mrp_resource_t *);

static void resource_class_move_resource(mrp_resmgr_class_t *,
                                         screen_resource_t *);
static uint32_t resource_key(mrp_resource_t *);


static void screen_notify(mrp_resource_event_t, mrp_zone_t *,
                          mrp_application_class_t *, mrp_resource_t *, void *);
static void screen_init(mrp_zone_t *, void *);
static bool screen_allocate(mrp_zone_t *, mrp_resource_t *, void *);
static void screen_free(mrp_zone_t *, mrp_resource_t *, void *);
static bool screen_advice(mrp_zone_t *, mrp_resource_t *, void *);
static void screen_commit(mrp_zone_t *, void *);


#define PRIORITY_ATTRIDX  0
#define CATHEGORY_ATTRIDX 1
#define APPID_ATTRIDX     2

static mrp_attr_def_t screen_attrs[] = {
    ATTRIBUTE("priority" , integer,       0      ),
    ATTRIBUTE("cathegory", integer,       0      ),
    ATTRIBUTE("appid"    , string , "<undefined>"),
    ATTR_END
};


static mrp_resource_mgr_ftbl_t screen_ftbl = {
    screen_notify,
    screen_init,
    screen_allocate,
    screen_free,
    screen_advice,
    screen_commit
};



mrp_resmgr_screen_t *mrp_resmgr_screen_create(mrp_resmgr_data_t *data)
{
    mrp_resmgr_screen_t *screen;
    uint32_t resid;
    uint32_t i;

    if ((screen = mrp_allocz(sizeof(*screen)))) {
        resid = mrp_resource_definition_create("screen", true, screen_attrs,
                                               &screen_ftbl, screen);
        mrp_lua_resclass_create_from_c(resid);

        screen->data = data;
        screen->resid = resid;

        for (i = 0;  i < MRP_ZONE_MAX;  i++)
            mrp_list_init(screen->classes + i);
    }

    return screen;
}


void mrp_resmgr_screen_destroy(mrp_resmgr_screen_t *screen)
{
    if (screen) {

        mrp_free(screen);
    }
}

static screen_resource_t *screen_resource_create(mrp_resmgr_screen_t *screen,
                                                 mrp_zone_t *zone,
                                                 mrp_resource_t *res,
                                                 mrp_application_class_t *ac)
{
    uint32_t zone_id;
    mrp_list_hook_t *classes;
    mrp_resmgr_class_t *rc;
    screen_resource_t *sr;

    MRP_ASSERT(screen && zone && res && ac, "invalid argument");
    MRP_ASSERT(screen->data, "confused with data structures");

    zone_id = mrp_zone_get_id(zone);
    classes = screen->classes + zone_id;

    if (!(rc = mrp_resmgr_class_find(classes, ac)) &&
        !(rc = mrp_resmgr_class_create(classes, ac)) )
    {
        mrp_log_error("ivi-resource-manager: can't obtain resmgr class");
    }
    else {
        if ((sr = mrp_allocz(sizeof(*sr)))) {
            mrp_list_init(&sr->link);
            sr->res = res;
            sr->screen = screen;
            sr->class = rc;
            sr->key = resource_key(res);

            printf("*** key 0x%08x\n", sr->key);

            resource_class_move_resource(rc, sr);

            mrp_resmgr_insert_resource(screen->data, res, sr);
        }
    }

    return sr;
}


static void screen_resource_destroy(mrp_resmgr_screen_t *screen,
                                    mrp_resource_t *res)
{
    screen_resource_t *sr;

    MRP_ASSERT(res && screen, "invalid argument");
    MRP_ASSERT(screen->data, "confused with data structures");

    if ((sr = mrp_resmgr_remove_resource(screen->data, res))) {
        mrp_list_delete(&sr->link);
        mrp_free(sr);
    }
}


static screen_resource_t *screen_resource_lookup(mrp_resmgr_screen_t *screen,
                                                 mrp_resource_t *res)
{
    screen_resource_t *sr;

    MRP_ASSERT(res && screen, "invalid argument");
    MRP_ASSERT(screen->data, "confused with data structures");

    sr = mrp_resmgr_lookup_resource(screen->data, res);

    return sr;
}

static void resource_class_move_resource(mrp_resmgr_class_t *class,
                                         screen_resource_t *resource)
{
    mrp_list_hook_t *list, *entry, *n, *insert_before;
    screen_resource_t *sr;

    mrp_list_delete(&resource->link);

    list = insert_before = &class->resources;

    mrp_list_foreach_back(list, entry, n) {
        sr = mrp_list_entry(entry, screen_resource_t, link);

        if (resource->key >= sr->key)
            break;

        insert_before = entry;
    }

    mrp_list_append(insert_before, &resource->link);
}

static uint32_t resource_key(mrp_resource_t *res)
{
    mrp_attr_t attr;
    uint32_t priority;
    uint32_t cathegory;
    uint32_t key = 0;

    do {
        if (!res)
            break;

        if (!mrp_resource_read_attribute(res, PRIORITY_ATTRIDX, &attr))
            break;
        if (attr.type != mqi_integer || attr.value.integer < 0)
            break;

        priority = attr.value.integer;

        if (!mrp_resource_read_attribute(res, CATHEGORY_ATTRIDX, &attr))
            break;
        if (attr.type != mqi_integer || attr.value.integer < 0)
            break;

        cathegory = attr.value.integer;

        key = ((cathegory & 0xffff) << 16) | (priority & 0xffff);

        return key;

    } while(0);

    return key;
}



static void screen_notify(mrp_resource_event_t event,
                          mrp_zone_t *zone,
                          mrp_application_class_t *ac,
                          mrp_resource_t *res,
                          void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);
    screen_resource_t *sr;

    MRP_ASSERT(zone && ac && res && screen, "invalid argument");

    switch (event) {

    case MRP_RESOURCE_EVENT_CREATED:
        mrp_log_info("screen resource in zone '%s' created", zone_name);
        screen_resource_create(screen, zone, res, ac);
        break;

    case MRP_RESOURCE_EVENT_DESTROYED:
        mrp_log_info("screen resource in zone '%s' destroyed", zone_name);
        screen_resource_destroy(screen, res);
        break;

    case MRP_RESOURCE_EVENT_ACQUIRE:
        mrp_log_info("screen resource in zone '%s' is acquiring", zone_name);
        if (!(sr = screen_resource_lookup(screen, res)))
            goto no_screen_resource;
        else {
            sr->active = true;
        }
        break;

    case MRP_RESOURCE_EVENT_RELEASE:
        mrp_log_info("screen resource in zone '%s' is released", zone_name);
        if (!(sr = screen_resource_lookup(screen, res)))
            goto no_screen_resource;
        else {
            sr->active = false;
        }
        break;

    no_screen_resource:
        mrp_log_error("ivi-resource-manager: can't find screen resource "
                      "in zone '%s'", zone_name);
        break;

    default:
        mrp_log_error("ivi-resource-manager: invalid event %d at screen "
                      "notification (zone '%s')", event, zone_name);
        break;
    }
}

static void screen_init(mrp_zone_t *zone, void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);

    MRP_ASSERT(screen, "invalid argument");

    mrp_log_info("screen init in zone '%s'", zone_name);
}

static bool screen_allocate(mrp_zone_t *zone,
                            mrp_resource_t *res,
                            void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);

    MRP_UNUSED(res);

    MRP_ASSERT(screen, "invalid argument");

    mrp_log_info("screen allocate in zone '%s'", zone_name);

    return TRUE;
}


static void screen_free(mrp_zone_t *zone, mrp_resource_t *res, void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);

    MRP_UNUSED(res);

    MRP_ASSERT(screen, "invalid argument");

    mrp_log_info("screen allocation free in zone '%s'", zone_name);
}

static bool screen_advice(mrp_zone_t *zone,mrp_resource_t *res,void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);

    MRP_UNUSED(res);

    MRP_ASSERT(screen, "invalid argument");

    mrp_log_info("screen advice in zone '%s'", zone_name);

    return TRUE;
}

static void screen_commit(mrp_zone_t *zone, void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);

    MRP_ASSERT(screen, "invalid argument");

    mrp_log_info("screen commit in zone '%s'", zone_name);
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
