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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <murphy/common.h>

#include <murphy-db/mqi.h>

#include <murphy/resource/config-api.h>
#include <murphy/resource/manager-api.h>

#include "screen.h"
#include "class.h"

#define RESOURCE_NAME       "screen"
#define ACTIVE_SCREEN_TABLE "active_screen"

#define ACTIVE_SCREEN_MAX   32

#define ATTRIBUTE(n,t,v)    {n, MRP_RESOURCE_RW, mqi_##t, {.t=v}}
#define ATTR_END            {NULL, 0, 0, {.string=NULL}}

typedef struct screen_resource_s   screen_resource_t;
typedef struct active_screen_s     active_screen_t;

struct active_screen_s {
    const char *appid;
};

struct mrp_resmgr_screen_s {
    mrp_resmgr_data_t *data;
    uint32_t resid;
    mrp_list_hook_t classes[MRP_ZONE_MAX];
    int nactive[MRP_ZONE_MAX];
    active_screen_t actives[MRP_ZONE_MAX][ACTIVE_SCREEN_MAX];
    mqi_handle_t dbtbl;
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
static void screen_resource_destroy(mrp_resmgr_screen_t *,  mrp_zone_t *,
                                    mrp_resource_t *);
static screen_resource_t *screen_resource_lookup(mrp_resmgr_screen_t *,
                                                 mrp_resource_t *);

static void resource_class_move_resource(mrp_resmgr_class_t *,
                                         screen_resource_t *);
static uint32_t resource_key(mrp_resource_t *);

static void get_active_screens(mrp_resmgr_screen_t *, mrp_zone_t *);

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
        resid = mrp_resource_definition_create(RESOURCE_NAME,true,screen_attrs,
                                               &screen_ftbl,screen);
        mrp_lua_resclass_create_from_c(resid);

        screen->data = data;
        screen->resid = resid;
        screen->dbtbl = MQI_HANDLE_INVALID;

        for (i = 0;  i < MRP_ZONE_MAX;  i++)
            mrp_list_init(screen->classes + i);

        mqi_open();

        mrp_resmgr_register_dependency(data, ACTIVE_SCREEN_TABLE);
    }

    return screen;
}


void mrp_resmgr_screen_destroy(mrp_resmgr_screen_t *screen)
{
    if (screen) {

        mrp_free(screen);
    }
}

int mrp_resmgr_screen_print(mrp_resmgr_screen_t *screen,
                            uint32_t zoneid,
                            char *buf, int len)
{
#define PRINT(...)                              \
    do {                                        \
        p += snprintf(p, e-p, __VA_ARGS__);     \
        if (p >= e)                             \
            return p - buf;                     \
    } while (0)

    char *p, *e;
    mrp_list_hook_t *classes, *centry, *cn;
    mrp_list_hook_t *resources, *rentry, *rn;
    mrp_resmgr_class_t *class;
    screen_resource_t *sr;
    const char *class_name;
    mrp_attr_def_t *attdef;
    mrp_attr_t a;
    int i;

    MRP_ASSERT(screen && buf && len > 0, "invalid argument");

    e = (p = buf) + len;
    classes = screen->classes + zoneid;

    PRINT("      Resource 'screen'\n");

    if (mrp_list_empty(classes))
        PRINT("         No resources\n");
    else {
        mrp_list_foreach(classes, centry, cn) {
            class = mrp_list_entry(centry, mrp_resmgr_class_t, link);
            class_name = mrp_application_class_get_name(class->class);
            resources = &class->resources;

            PRINT("         Class '%s':\n", class_name);

            mrp_list_foreach(resources, rentry, rn) {
                sr = mrp_list_entry(rentry, screen_resource_t, link);

                PRINT("            0x%08x %sactive",
                      sr->key, sr->active ? "  ":"in");

                for (i = 0;    i < MRP_ARRAY_SIZE(screen_attrs) - 1;    i++) {
                    if ((mrp_resource_read_attribute(sr->res, i, &a))) {
                        PRINT(" %s:", a.name);

                        switch (a.type) {
                        case mqi_string:   PRINT("'%s'",a.value.string); break;
                        case mqi_integer:  PRINT("%ld",a.value.integer); break;
                        case mqi_unsignd:  PRINT("%ld",a.value.unsignd); break;
                        case mqi_floating: PRINT("%lf",a.value.floating);break;
                        default:           PRINT("<unsupported type>");  break;
                        }
                    }
                }

                PRINT("\n");
            }
        }
    }

    return p - buf;

#undef PRINT
}

static screen_resource_t *screen_resource_create(mrp_resmgr_screen_t *screen,
                                                 mrp_zone_t *zone,
                                                 mrp_resource_t *res,
                                                 mrp_application_class_t *ac)
{
    mrp_resmgr_data_t *data;
    uint32_t zone_id;
    mrp_list_hook_t *classes;
    mrp_resmgr_class_t *rc;
    screen_resource_t *sr;

    MRP_ASSERT(screen && zone && res && ac, "invalid argument");
    MRP_ASSERT(screen->data, "confused with data structures");

    data = screen->data;

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

            mrp_resmgr_insert_resource(data, zone, res, sr);
        }
    }

    return sr;
}


static void screen_resource_destroy(mrp_resmgr_screen_t *screen,
                                    mrp_zone_t *zone,
                                    mrp_resource_t *res)
{
    screen_resource_t *sr;

    MRP_ASSERT(res && screen, "invalid argument");
    MRP_ASSERT(screen->data, "confused with data structures");

    if ((sr = mrp_resmgr_remove_resource(screen->data, zone, res))) {
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

static void get_active_screens(mrp_resmgr_screen_t *screen, mrp_zone_t *zone)
{
    static const char *zone_name;

    MQI_COLUMN_SELECTION_LIST(columns,
        MQI_COLUMN_SELECTOR( 1, active_screen_t, appid )
    );

    MQI_WHERE_CLAUSE(where,
        MQI_EQUAL( MQI_COLUMN(0), MQI_STRING_VAR(zone_name) )
    );


    uint32_t zone_id;
    int n, nrow;
    active_screen_t rows[MRP_ZONE_MAX * ACTIVE_SCREEN_MAX];
    active_screen_t *as, *from, *to;
    int i;

    if (!screen || !zone)
        return;

    zone_id = mrp_zone_get_id(zone);
    zone_name = mrp_zone_get_name(zone);

    for (i = 0, n = screen->nactive[zone_id];   i < n;   i++) {
        as = &screen->actives[zone_id][i];
        mrp_free((void *)as->appid);
        as = NULL;
    }

    screen->nactive[zone_id] = 0;

    if (screen->dbtbl == MQI_HANDLE_INVALID) {
        screen->dbtbl = mqi_get_table_handle(ACTIVE_SCREEN_TABLE);

        if (screen->dbtbl == MQI_HANDLE_INVALID)
            return;

        mrp_log_info("ivi-resource-manager: 'active_screen' table found");
    }

    if ((size_t)mqi_get_table_size(screen->dbtbl) > MRP_ARRAY_SIZE(rows)) {
        mrp_log_error("ivi-resource-manager: table size exceeds the max.");
        return;
    }

    if ((nrow = MQI_SELECT(columns, screen->dbtbl, where, rows)) < 0) {
        mrp_log_error("ivi-resource-manager: DB select failed: %s",
                      strerror(errno));
        return;
    }

    if (nrow > ACTIVE_SCREEN_MAX) {
        mrp_log_error("ivi-resource-manager: DB select result is too "
                      "large (%d). Will be truncated to %d",
                      nrow, ACTIVE_SCREEN_MAX);
        nrow = ACTIVE_SCREEN_MAX;
    }

    for (i = 0;  i < nrow;  i++) {
        from = &rows[i];
        to = &screen->actives[zone_id][i];

        to->appid = mrp_strdup(from->appid);
    }

    screen->nactive[zone_id] = nrow;
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
        screen_resource_destroy(screen, zone, res);
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

    get_active_screens(screen, zone);
}

static bool screen_allocate(mrp_zone_t *zone,
                            mrp_resource_t *res,
                            void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);
    uint32_t zone_id = mrp_zone_get_id(zone);
    mrp_attr_t attr;
    const char *appid;
    active_screen_t *as;
    int i, n;

    MRP_ASSERT(res && screen, "invalid argument");

    if (!mrp_resource_read_attribute(res, APPID_ATTRIDX, &attr)) {
        mrp_log_error("screen allocate in zone '%s' failed: "
                      "attribute read error", zone_name);
        return FALSE;
    }

    appid = attr.value.string;


    mrp_log_info("screen allocate in zone '%s' for '%s'", zone_name, appid);

    for (i = 0, n = screen->nactive[zone_id];   i < n;   i++) {
        as = &screen->actives[zone_id][i];

        if (!strcmp(appid, as->appid))
            return TRUE;
    }

    return FALSE;
}


static void screen_free(mrp_zone_t *zone, mrp_resource_t *res, void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);

    MRP_UNUSED(res);

    MRP_ASSERT(screen, "invalid argument");

    mrp_log_info("screen free in zone '%s'", zone_name);
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
