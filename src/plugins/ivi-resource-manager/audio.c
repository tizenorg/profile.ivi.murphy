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
#include <murphy/resource/client-api.h>

#include "audio.h"
#include "class.h"
#include "appid.h"

#define RESOURCE_NAME       "audio_playback"
#define ACTIVE_SCREEN_TABLE "active_screen"

#define ACTIVE_SCREEN_MAX   32

#define BIT(i)               ((uint32_t)1 << (i))
#define MASK(w)              (((uint32_t)1 << (w)) - 1)

#define PRIORITY_BITS       8
#define CATEGORY_BITS       8
#define ZORDER_BITS         16

#define PRIORITY_POSITION   0
#define CATEGORY_POSITION   (PRIORITY_POSITION + PRIORITY_BITS)
#define ZORDER_POSITION     (CATEGORY_POSITION + CATEGORY_BITS)

#define PRIORITY_MASK       MASK(PRIORITY_BITS)
#define CATEGORY_MASK       MASK(CATEGORY_BITS)
#define ZORDER_MASK         MASK(ZORDER_BITS)

#define STAMP_BITS          30
#define ACQUIRE_BITS        1
#define SHARE_BITS          1

#define STAMP_POSITION      0
#define ACQUIRE_POSITION    (STAMP_POSITION + STAMP_BITS)
#define SHARE_POSITION      (ACQUIRE_POSITION + ACQUIRE_BITS)

#define STAMP_MASK          MASK(STAMP_BITS)
#define ACQUIRE_MASK        MASK(ACQUIRE_BITS)
#define SHARE_MASK          MASK(SHARE_BITS)


#define ATTRIBUTE(n,t,v)    {n, MRP_RESOURCE_RW, mqi_##t, {.t=v}}
#define ATTR_END            {NULL, 0, 0, {.string=NULL}}

typedef struct audio_resource_s   audio_resource_t;
typedef struct active_screen_s    active_screen_t;

struct active_screen_s {
    const char *appid;
};

struct mrp_resmgr_audio_s {
    mrp_resmgr_data_t *data;
    uint32_t resid;
    mrp_list_hook_t classes[MRP_ZONE_MAX];
    int nactive[MRP_ZONE_MAX];
    active_screen_t actives[MRP_ZONE_MAX][ACTIVE_SCREEN_MAX];
    mqi_handle_t dbtbl;
    uint32_t grantids[MRP_ZONE_MAX];
};

struct audio_resource_s {
    mrp_list_hook_t link;
    mrp_resource_t *res;
    mrp_resmgr_audio_t *audio;
    mrp_resmgr_class_t *class;
    bool acquire;
    uint32_t grantid;
    uint32_t key;
};


static audio_resource_t *audio_resource_create(mrp_resmgr_audio_t *,
                                                 mrp_zone_t *,mrp_resource_t *,
                                                 mrp_application_class_t *);
static void audio_resource_destroy(mrp_resmgr_audio_t *,  mrp_zone_t *,
                                    mrp_resource_t *);
static audio_resource_t *audio_resource_lookup(mrp_resmgr_audio_t *,
                                                 mrp_resource_t *);
static void audio_update_resources(mrp_resmgr_audio_t *, mrp_zone_t *);
static void audio_grant_resources(mrp_resmgr_audio_t *, mrp_zone_t *);

static void resource_class_move_resource(mrp_resmgr_class_t *,
                                         audio_resource_t *);
static uint32_t resource_key(audio_resource_t *);
static bool resource_is_active(mrp_resmgr_audio_t *, uint32_t,
                               audio_resource_t *);
static void resource_fix_appid(mrp_resmgr_audio_t *, mrp_resource_t *);

static void get_active_screens(mrp_resmgr_audio_t *, mrp_zone_t *);

static void audio_notify(mrp_resource_event_t, mrp_zone_t *,
                          mrp_application_class_t *, mrp_resource_t *, void *);
static void audio_init(mrp_zone_t *, void *);
static bool audio_allocate(mrp_zone_t *, mrp_resource_t *, void *);
static void audio_free(mrp_zone_t *, mrp_resource_t *, void *);
static bool audio_advice(mrp_zone_t *, mrp_resource_t *, void *);
static void audio_commit(mrp_zone_t *, void *);


#define PRIORITY_ATTRIDX  0
#define CATEGORY_ATTRIDX  1
#define APPID_ATTRIDX     2
#define ROLE_ATTRIDX      3
#define PID_ATTRIDX       4
#define POLICY_ATTRIDX    5

static mrp_attr_def_t audio_attrs[] = {
    ATTRIBUTE("priority" , integer,       0      ),
    ATTRIBUTE("cathegory", integer,       0      ),
    ATTRIBUTE("appid"    , string , "<undefined>"),
    ATTRIBUTE("role"     , string , "music"      ),
    ATTRIBUTE("pid"      , string , "<unknown>"  ),
    ATTRIBUTE("policy"   , string , "relaxed"    ),
    ATTR_END
};


static mrp_resource_mgr_ftbl_t audio_ftbl = {
    audio_notify,
    audio_init,
    audio_allocate,
    audio_free,
    audio_advice,
    audio_commit
};


static uint32_t request_stamp;


mrp_resmgr_audio_t *mrp_resmgr_audio_create(mrp_resmgr_data_t *data)
{
    mrp_resmgr_audio_t *audio;
    uint32_t resid;
    uint32_t i;

    if ((audio = mrp_allocz(sizeof(*audio)))) {
        resid = mrp_resource_definition_create(RESOURCE_NAME,true,audio_attrs,
                                               &audio_ftbl,audio);
        mrp_lua_resclass_create_from_c(resid);

        audio->data = data;
        audio->resid = resid;
        audio->dbtbl = MQI_HANDLE_INVALID;

        for (i = 0;  i < MRP_ZONE_MAX;  i++)
            mrp_list_init(audio->classes + i);

        mqi_open();

        mrp_resmgr_register_dependency(data, ACTIVE_SCREEN_TABLE);
    }

    return audio;
}


void mrp_resmgr_audio_destroy(mrp_resmgr_audio_t *audio)
{
    if (audio) {

        mrp_free(audio);
    }
}

int mrp_resmgr_audio_print(mrp_resmgr_audio_t *audio,
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
    audio_resource_t *ar;
    const char *class_name;
    mrp_attr_t a;
    int i;

    MRP_ASSERT(audio && buf && len > 0, "invalid argument");

    e = (p = buf) + len;
    classes = audio->classes + zoneid;

    PRINT("      Resource 'audio' - grantid:%u\n", audio->grantids[zoneid]);

    if (mrp_list_empty(classes))
        PRINT("         No resources\n");
    else {
        mrp_list_foreach_back(classes, centry, cn) {
            class = mrp_list_entry(centry, mrp_resmgr_class_t, link);
            class_name = mrp_application_class_get_name(class->class);
            resources = &class->resources;

            PRINT("         Class '%s':\n", class_name);

            mrp_list_foreach_back(resources, rentry, rn) {
                ar = mrp_list_entry(rentry, audio_resource_t, link);

                PRINT("            0x%08x %s %u",
                      ar->key, ar->acquire ? "acquire":"release", ar->grantid);

                for (i = 0;  i < (int)MRP_ARRAY_SIZE(audio_attrs) - 1;  i++) {
                    if ((mrp_resource_read_attribute(ar->res, i, &a))) {
                        PRINT(" %s:", a.name);

                        switch (a.type) {
                        case mqi_string:   PRINT("'%s'",a.value.string); break;
                        case mqi_integer:  PRINT("%d",a.value.integer);  break;
                        case mqi_unsignd:  PRINT("%u",a.value.unsignd);  break;
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

static audio_resource_t *audio_resource_create(mrp_resmgr_audio_t *audio,
                                               mrp_zone_t *zone,
                                               mrp_resource_t *res,
                                               mrp_application_class_t *ac)
{
    mrp_resmgr_data_t *data;
    uint32_t zone_id;
    mrp_list_hook_t *classes;
    mrp_resmgr_class_t *rc;
    audio_resource_t *ar;

    MRP_ASSERT(audio && zone && res && ac, "invalid argument");
    MRP_ASSERT(audio->data, "confused with data structures");

    data = audio->data;

    zone_id = mrp_zone_get_id(zone);
    classes = audio->classes + zone_id;

    if (!(rc = mrp_resmgr_class_find(classes, ac)) &&
        !(rc = mrp_resmgr_class_create(classes, ac)) )
    {
        mrp_log_error("ivi-resource-manager: can't obtain resmgr class");
    }
    else {
        resource_fix_appid(audio, res);

        if ((ar = mrp_allocz(sizeof(*ar)))) {
            mrp_list_init(&ar->link);
            ar->res = res;
            ar->audio = audio;
            ar->class = rc;
            ar->key = resource_key(ar);

            resource_class_move_resource(rc, ar);

            mrp_resmgr_insert_resource(data, zone, res, ar);
        }
    }

    return ar;
}


static void audio_resource_destroy(mrp_resmgr_audio_t *audio,
                                    mrp_zone_t *zone,
                                    mrp_resource_t *res)
{
    audio_resource_t *ar;

    MRP_ASSERT(res && audio, "invalid argument");
    MRP_ASSERT(audio->data, "confused with data structures");

    if ((ar = mrp_resmgr_remove_resource(audio->data, zone, res))) {
        mrp_list_delete(&ar->link);
        mrp_free(ar);
    }
}


static audio_resource_t *audio_resource_lookup(mrp_resmgr_audio_t *audio,
                                                 mrp_resource_t *res)
{
    audio_resource_t *ar;

    MRP_ASSERT(res && audio, "invalid argument");
    MRP_ASSERT(audio->data, "confused with data structures");

    ar = mrp_resmgr_lookup_resource(audio->data, res);

    return ar;
}

static void audio_update_resources(mrp_resmgr_audio_t *audio,
                                    mrp_zone_t *zone)
{
    uint32_t zoneid;
    mrp_list_hook_t *classes, *centry, *cn;
    mrp_list_hook_t *resources, *rentry, *rn;
    mrp_resmgr_class_t *class;
    const char *class_name;
    audio_resource_t *ar, *ars[4096];
    int nar;
    uint32_t zorder;
    int i;

    zoneid = mrp_zone_get_id(zone);
    classes = audio->classes + zoneid;

    mrp_list_foreach_back(classes, centry, cn) {
        class = mrp_list_entry(centry, mrp_resmgr_class_t, link);
        class_name = mrp_application_class_get_name(class->class);
        resources = &class->resources;

        if (!strcmp(class_name, "player") || !strcmp(class_name, "base")) {
            nar = 0;

            mrp_list_foreach_back(resources, rentry, rn) {
                ar = mrp_list_entry(rentry, audio_resource_t, link);

                if (resource_is_active(audio, zoneid, ar)) {
                    ar->key |= (ZORDER_MASK << ZORDER_POSITION);
                    if (nar >= (int)MRP_ARRAY_SIZE(ars)) {
                        mrp_log_error("ivi-resource-manager: "
                                      "too many active audios");
                        break;
                    }
                    mrp_list_delete(&ar->link);
                    ars[nar++] = ar;
                }
                else {
                    zorder  = (ar->key >> ZORDER_POSITION) & ZORDER_MASK;
                    zorder -= (zorder > 0) ? 1 : 0;

                    ar->key &= ~(ZORDER_MASK << ZORDER_POSITION);
                    ar->key |= (zorder << ZORDER_POSITION);
                }
            } /* foreach resource */

            for (i = 0;   i < nar;   i++)
                resource_class_move_resource(class, ars[i]);
        } /* if class is 'player' or 'base' */

    } /* foreach class */
}

static void audio_grant_resources(mrp_resmgr_audio_t *audio,
                                   mrp_zone_t *zone)
{
    uint32_t zoneid;
    uint32_t grantid;
    mrp_list_hook_t *classes, *centry, *cn;
    mrp_list_hook_t *resources, *rentry, *rn;
    mrp_resmgr_class_t *class;
    const char *class_name;
    bool base_class;
    audio_resource_t *ar;

    zoneid  = mrp_zone_get_id(zone);
    classes = audio->classes + zoneid;
    grantid = ++audio->grantids[zoneid];

    mrp_list_foreach_back(classes, centry, cn) {
        class = mrp_list_entry(centry, mrp_resmgr_class_t, link);
        class_name = mrp_application_class_get_name(class->class);
        resources = &class->resources;
        base_class = !strcmp(class_name, "player") ||
            !strcmp(class_name, "base");

        if (!base_class || audio->nactive[zoneid]) {
            mrp_list_foreach_back(resources, rentry, rn) {
                ar = mrp_list_entry(rentry, audio_resource_t, link);

                if (!ar->acquire)
                    continue;

                if (base_class && !resource_is_active(audio,zoneid,ar))
                    continue;

                ar->grantid = grantid;

                if (!mrp_resource_is_shared(ar->res))
                    return;
            } /* resources */
        }
    }
}

static void resource_class_move_resource(mrp_resmgr_class_t *class,
                                         audio_resource_t *resource)
{
    mrp_list_hook_t *list, *entry, *n, *insert_before;
    audio_resource_t *ar;

    mrp_list_delete(&resource->link);

    list = insert_before = &class->resources;

    mrp_list_foreach_back(list, entry, n) {
        ar = mrp_list_entry(entry, audio_resource_t, link);

        if (resource->key >= ar->key)
            break;

        insert_before = entry;
    }

    mrp_list_append(insert_before, &resource->link);
}


static uint32_t resource_key(audio_resource_t *ar)
{
    mrp_resmgr_class_t *class;
    const char *class_name;
    bool base_class;
    mrp_resource_t *res;
    mrp_attr_t a;
    uint32_t priority;
    uint32_t category;
    bool share;
    bool acquire;
    uint32_t key = 0;

    do {
        if (!(class = ar->class))
            base_class = false;
        else {
            class_name = mrp_application_class_get_name(class->class);
            base_class = !strcmp(class_name, "player") ||
                         !strcmp(class_name, "base");
        }

        if (!(res = ar->res))
            break;

        if (base_class) {
            if (!mrp_resource_read_attribute(res, PRIORITY_ATTRIDX, &a))
                break;
            if (a.type != mqi_integer || a.value.integer < 0)
                break;

            priority = (a.value.integer & PRIORITY_MASK) << PRIORITY_POSITION;

            if (!mrp_resource_read_attribute(res, CATEGORY_ATTRIDX, &a))
                break;
            if (a.type != mqi_integer || a.value.integer < 0)
                break;

            category = (a.value.integer & CATEGORY_MASK) << CATEGORY_POSITION;

            key = (priority | category);
        }
        else {
            acquire = ar->acquire;
            share = mrp_resource_is_shared(res);

            key  = (++request_stamp & STAMP_MASK);
            key |= (acquire ? (ACQUIRE_BITS << ACQUIRE_POSITION) : 0);
            key |= (share ? (SHARE_BITS << SHARE_POSITION) : 0);
        }

    } while(0);

    return key;
}

static bool resource_is_active(mrp_resmgr_audio_t *audio,
                               uint32_t zoneid,
                               audio_resource_t *ar)
{
    active_screen_t *as;
    mrp_attr_t a;
    const char *appid;
    int i, n;

    if (mrp_resource_read_attribute(ar->res, APPID_ATTRIDX, &a)) {
        appid = a.value.string;

        for (i = 0, n = audio->nactive[zoneid];   i < n;   i++) {
            as = &audio->actives[zoneid][i];

            if (!strcmp(appid, as->appid))
                return true;
        }
    }

    return false;
}

static void resource_fix_appid(mrp_resmgr_audio_t *audio, mrp_resource_t *res)
{
    mrp_resmgr_data_t *data;
    mrp_attr_t attr, attrs[2];
    const char *appid;
    const char *pid;

    data = audio->data;
    appid = NULL;
    pid = 0;

    if (mrp_resource_read_attribute(res, PID_ATTRIDX, &attr)) {
        if (attr.type == mqi_string) {
            if (strcmp(attr.value.string, "<unknown>"))
                pid = attr.value.string;
        }
    }

    if (mrp_resource_read_attribute(res, APPID_ATTRIDX, &attr)) {
        if (attr.type == mqi_string) {
            if (strcmp(attr.value.string, "<undefined>"))
                appid = attr.value.string;
        }
    }

    if (!appid && pid) {
        appid = mrp_resmgr_appid_find_by_pid(mrp_resmgr_get_appid(data), pid);

        if (appid) {
            memset(attrs, 0, sizeof(attrs));
            attrs[0].name = audio_attrs[APPID_ATTRIDX].name;
            attrs[0].type = mqi_string;
            attrs[0].value.string = appid;

            mrp_resource_write_attributes(res, attrs);
        }
    }
}

static void get_active_screens(mrp_resmgr_audio_t *audio, mrp_zone_t *zone)
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

    if (!audio || !zone)
        return;

    zone_id = mrp_zone_get_id(zone);
    zone_name = mrp_zone_get_name(zone);

    for (i = 0, n = audio->nactive[zone_id];   i < n;   i++) {
        as = &audio->actives[zone_id][i];
        mrp_free((void *)as->appid);
        as = NULL;
    }

    audio->nactive[zone_id] = 0;

    if (audio->dbtbl == MQI_HANDLE_INVALID) {
        audio->dbtbl = mqi_get_table_handle(ACTIVE_SCREEN_TABLE);

        if (audio->dbtbl == MQI_HANDLE_INVALID)
            return;

        mrp_log_info("ivi-resource-manager: audio resource: "
                     "'active_screen' table found");
    }

    if ((size_t)mqi_get_table_size(audio->dbtbl) > MRP_ARRAY_SIZE(rows)) {
        mrp_log_error("ivi-resource-manager: audio resource: "
                      "table size exceeds the max.");
        return;
    }

    if ((nrow = MQI_SELECT(columns, audio->dbtbl, where, rows)) < 0) {
        mrp_log_error("ivi-resource-manager: audio resource: "
                      "DB select failed: %s", strerror(errno));
        return;
    }

    if (nrow > ACTIVE_SCREEN_MAX) {
        mrp_log_error("ivi-resource-manager: audio resource: "
                      "DB select result is too large (%d). "
                      "Will be truncated to %d", nrow, ACTIVE_SCREEN_MAX);
        nrow = ACTIVE_SCREEN_MAX;
    }

    for (i = 0;  i < nrow;  i++) {
        from = &rows[i];
        to = &audio->actives[zone_id][i];

        to->appid = mrp_strdup(from->appid);
    }

    audio->nactive[zone_id] = nrow;
}


static void audio_notify(mrp_resource_event_t event,
                          mrp_zone_t *zone,
                          mrp_application_class_t *ac,
                          mrp_resource_t *res,
                          void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);
    audio_resource_t *ar;

    MRP_ASSERT(zone && ac && res && audio, "invalid argument");

    switch (event) {

    case MRP_RESOURCE_EVENT_CREATED:
        mrp_log_info("audio resource in zone '%s' created", zone_name);
        audio_resource_create(audio, zone, res, ac);
        break;

    case MRP_RESOURCE_EVENT_DESTROYED:
        mrp_log_info("audio resource in zone '%s' destroyed", zone_name);
        audio_resource_destroy(audio, zone, res);
        break;

    case MRP_RESOURCE_EVENT_ACQUIRE:
        mrp_log_info("audio resource in zone '%s' is acquiring", zone_name);
        if (!(ar = audio_resource_lookup(audio, res)))
            goto no_audio_resource;
        else {
            ar->acquire = true;
        }
        break;

    case MRP_RESOURCE_EVENT_RELEASE:
        mrp_log_info("audio resource in zone '%s' is released", zone_name);
        if (!(ar = audio_resource_lookup(audio, res)))
            goto no_audio_resource;
        else {
            ar->acquire = false;
        }
        break;

    no_audio_resource:
        mrp_log_error("ivi-resource-manager: can't find audio resource "
                      "in zone '%s'", zone_name);
        break;

    default:
        mrp_log_error("ivi-resource-manager: invalid event %d at audio "
                      "notification (zone '%s')", event, zone_name);
        break;
    }
}

static void audio_init(mrp_zone_t *zone, void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);

    MRP_ASSERT(audio, "invalid argument");

    mrp_log_info("audio init in zone '%s'", zone_name);

    get_active_screens(audio, zone);
    audio_update_resources(audio, zone);

    audio_grant_resources(audio, zone);
}

static bool audio_allocate(mrp_zone_t *zone,
                            mrp_resource_t *res,
                            void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    uint32_t zoneid;
    const char *zonenam = mrp_zone_get_name(zone);
    audio_resource_t *ar;
    uint32_t grantid;

    MRP_ASSERT(res && audio, "invalid argument");

    zoneid  = mrp_zone_get_id(zone);
    zonenam = mrp_zone_get_name(zone);
    grantid = audio->grantids[zoneid];

    mrp_log_info("audio allocate in zone '%s'", zonenam);

    if ((ar = audio_resource_lookup(audio, res))) {
        return (ar->grantid == grantid);
    }

    mrp_log_error("ivi-resource-manager: attempt to allocate "
                  "untracked resource");

    return FALSE;
}


static void audio_free(mrp_zone_t *zone, mrp_resource_t *res, void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);

    MRP_UNUSED(res);

    MRP_ASSERT(audio, "invalid argument");

    mrp_log_info("audio free in zone '%s'", zone_name);
}

static bool audio_advice(mrp_zone_t *zone,mrp_resource_t *res,void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);

    MRP_UNUSED(res);

    MRP_ASSERT(audio, "invalid argument");

    mrp_log_info("audio advice in zone '%s'", zone_name);

    return TRUE;
}

static void audio_commit(mrp_zone_t *zone, void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    const char *zone_name = mrp_zone_get_name(zone);

    MRP_ASSERT(audio, "invalid argument");

    mrp_log_info("audio commit in zone '%s'", zone_name);
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
