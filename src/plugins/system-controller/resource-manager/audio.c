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
#include "notifier.h"
#include "wayland/wayland.h"
#include "application/application.h"

#define ANY_ZONE  (~((uint32_t)0))

#ifdef BIT
#undef BIT
#endif
#ifdef MASK
#undef MASK
#endif
#ifdef MAX
#undef MAX
#endif

#define BIT(i)                ((uint32_t)1 << (i))
#define MASK(w)               (((uint32_t)1 << (w)) - 1)
#define MAX(w)                (((uint32_t)1 << (w)))

#define STAMP_BITS            13
#define PRIORITY_BITS         8
#define CLASSPRI_BITS         8
#define ACQUIRE_BITS          1
#define SHARE_BITS            1
#define INTERRUPT_BITS        1

#define STAMP_POSITION        0
#define PRIORITY_POSITION     (STAMP_POSITION + STAMP_BITS)
#define CLASSPRI_POSITION     (PRIORITY_POSITION + PRIORITY_BITS)
#define ACQUIRE_POSITION      (CLASSPRI_POSITION + CLASSPRI_BITS)
#define SHARE_POSITION        (ACQUIRE_POSITION + ACQUIRE_BITS)
#define INTERRUPT_POSITION    (SHARE_POSITION + SHARE_BITS)

#define STAMP_MASK            MASK(STAMP_BITS)
#define PRIORITY_MASK         MASK(PRIORITY_BITS)
#define CLASSPRI_MASK         MASK(CLASSPRI_BITS)
#define ACQUIRE_MASK          MASK(ACQUIRE_BITS)
#define SHARE_MASK            MASK(SHARE_BITS)
#define INTERRUPT_MASK        MASK(INTERRUPT_BITS)

#define STAMP_MAX             MAX(STAMP_BITS)
#define PRIORITY_MAX          MAX(PRIORITY_BITS)
#define CLASSPRI_MAX          MAX(CLASSPRI_BITS)
#define ACQUIRE_MAX           MAX(ACQUIRE_BITS)
#define SHARE_MAX             MAX(SHARE_BITS)
#define INTERRUPT_MAX         MAX(INTERRUPT_BITS)


#define ATTRIBUTE(n,t,v)    {n, MRP_RESOURCE_RW, mqi_##t, {.t=v}}
#define ATTR_END            {NULL, 0, 0, {.string=NULL}}

typedef struct audio_resource_s    audio_resource_t;
typedef struct disable_iterator_s  disable_iterator_t;

struct audio_resource_s {
    mrp_list_hook_t link;
    mrp_resmgr_audio_t *audio;
    mrp_resource_t *res;
    uint32_t zoneid;
    uint32_t audioid; /* unique id. Not to confuse with resource-set id */
    bool interrupt;
    uint32_t classpri;
    uint32_t key;
    bool acquire;
    bool grant;
    uint32_t grantid;
    mrp_application_requisite_t requisite;
    mrp_resmgr_disable_t disable;
};

struct disable_iterator_s {
    uint32_t zoneid;
    bool disable;
    mrp_resmgr_disable_t type;
    uint32_t mask;
    union {
        const char *appid;
        mrp_application_requisite_t req;
    };
    uint32_t zones;
    int counter;
};

static int hash_compare(const void *, const void *);
static uint32_t hash_function(const void *);

static const char *get_appid_for_resource(mrp_resource_t *);
static mrp_application_t *get_application_for_resource(mrp_resource_t *);
static uint32_t get_priority_for_resource(mrp_resource_t *);
static uint32_t get_class_priority_for_resource(mrp_resource_t *);


static audio_resource_t *audio_resource_create(mrp_resmgr_audio_t *,
                                                 mrp_zone_t *,mrp_resource_t *,
                                                 mrp_application_class_t *);
static void audio_resource_destroy(mrp_resmgr_audio_t *,  mrp_zone_t *,
                                    mrp_resource_t *);
static audio_resource_t *audio_resource_lookup(mrp_resmgr_audio_t *,
                                                 mrp_resource_t *);

static void audio_insert_resource(audio_resource_t *);
static void audio_grant_resources(mrp_resmgr_audio_t *, mrp_zone_t *);
static void audio_queue_events(mrp_resmgr_audio_t *, mrp_zone_t *);

static uint32_t resource_key(audio_resource_t *);

static void audio_notify(mrp_resource_event_t, mrp_zone_t *,
                          mrp_application_class_t *, mrp_resource_t *, void *);
static void audio_init(mrp_zone_t *, void *);
static bool audio_allocate(mrp_zone_t *, mrp_resource_t *, void *);
static void audio_free(mrp_zone_t *, mrp_resource_t *, void *);
static bool audio_advice(mrp_zone_t *, mrp_resource_t *, void *);
static void audio_commit(mrp_zone_t *, void *);

#define PRIORITY_ATTRIDX  0
#define CLASSPRI_ATTRIDX  1
#define APPID_ATTRIDX     2
#define ROLE_ATTRIDX      3
#define PID_ATTRIDX       4
#define POLICY_ATTRIDX    5

static mrp_attr_def_t audio_attrs[] = {
    ATTRIBUTE("priority" , integer,       0      ),
    ATTRIBUTE("classpri" , integer,      -1      ),
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

mrp_resmgr_audio_t *mrp_resmgr_audio_create(mrp_resmgr_t *resmgr)
{
    mrp_resmgr_audio_t *audio;
    mrp_htbl_config_t cfg;
    uint32_t resid;
    size_t i;

    if ((audio = mrp_allocz(sizeof(mrp_resmgr_audio_t)))) {
        resid = mrp_resource_definition_create(MRP_SYSCTL_AUDIO_RESOURCE,
                                               true,audio_attrs,
                                               &audio_ftbl, audio);
        mrp_lua_resclass_create_from_c(resid);

        cfg.nentry = MRP_RESMGR_RESOURCE_MAX;
        cfg.comp = hash_compare;
        cfg.hash = hash_function;
        cfg.free = NULL;
        cfg.nbucket = MRP_RESMGR_RESOURCE_BUCKETS;

        audio->resmgr = resmgr;
        audio->resid = resid;
        audio->resources = mrp_htbl_create(&cfg);

        for (i = 0;  i < MRP_ZONE_MAX;  i++)
            mrp_list_init(audio->zones + i);
    }

    return audio;
}

void mrp_resmgr_audio_destroy(mrp_resmgr_audio_t *audio)
{
    if (audio) {
        mrp_free(audio);
    }
}

static int audio_disable_cb(void *key, void *object, void *user_data)
{
    audio_resource_t *ar = (audio_resource_t *)object;
    disable_iterator_t *it = (disable_iterator_t *)user_data;
    const char *appid;
    uint32_t disable;

    MRP_UNUSED(key);

    MRP_ASSERT(ar && it, "invalid argument");

    if (it->zoneid == ANY_ZONE || ar->zoneid == it->zoneid) {
        switch (it->type) {

        case MRP_RESMGR_DISABLE_REQUISITE:
            if (it->req && (it->req & ar->requisite) == it->req)
                goto disable;
            break;

        case MRP_RESMGR_DISABLE_APPID:
            if ((appid = get_appid_for_resource(ar->res)) &&
                it->appid && !strcmp(it->appid, appid))
                goto disable;
            break;

        disable:
            disable = ar->disable & it->mask;
            if (it->disable) {
                if (disable)
                    break;
                ar->disable |= it->mask;
            }
            else {
                if (!disable)
                    break;
                ar->disable &= ~it->mask;
            }
            it->counter++;
            it->zones |= (((uint32_t)1) << ar->zoneid);
            break;

        default:
            return MRP_HTBL_ITER_STOP;
        }
    }
    
    return MRP_HTBL_ITER_MORE;
}

int mrp_resmgr_audio_disable(mrp_resmgr_audio_t *audio,
                             const char *zone_name,
                             bool disable,
                             mrp_resmgr_disable_t type,
                             void *data)
{
    const char *zone_names[MRP_ZONE_MAX + 1];
    disable_iterator_t it;
    uint32_t i;
    uint32_t zone_id = ANY_ZONE;
    uint32_t mask;
    uint32_t z;

    MRP_ASSERT(audio && data && zone_name, "invalid argument");

    mrp_debug("zone_name='%s' %s, type=0x%02x data=%p",
              zone_name ? zone_name : "<any zone>",
              disable ? "disable" : "enable",
              type, data);

    if (zone_name && strcmp(zone_name, "*")) {
        if (mrp_zone_get_all_names(MRP_ZONE_MAX + 1, zone_names)) {
            for (i = 0;  zone_names[i];  i++) {
                if (!strcmp(zone_name, zone_names[i])) {
                    zone_id = i;
                    break;
                }
            }
        }
        if (zone_id == ANY_ZONE) {
            mrp_log_error("system-controller: failed to disable audio: "
                          "can't find zone '%s'", zone_name);
            return -1;
        }
    }


    memset(&it, 0, sizeof(it));
    it.zoneid = zone_id;
    it.disable = disable;
    it.type = type;
    it.mask = BIT(type - 1);
    it.zones = 0;
    it.counter = 0;
    
    switch (type) {

    case MRP_RESMGR_DISABLE_REQUISITE:
        it.req = *(mrp_application_requisite_t *)data;
        break;

    case MRP_RESMGR_DISABLE_APPID:
        it.appid = (const char *)data;
        break;

    default:
        mrp_log_error("system-controller: invalid type %d of "
                      "audio disable", type);
        return -1;
    }

    mrp_htbl_foreach(audio->resources, audio_disable_cb, &it);

    for (z = 0;   it.zones && z < MRP_ZONE_MAX;   z++) {
        mask = (((uint32_t)1) << z);

        if ((mask & it.zones)) {
            it.zones &= ~mask;
            mrp_resource_owner_recalc(z);
        }
    }

    return it.counter;
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
    uint32_t grantid;
    mrp_list_hook_t *resources, *rentry, *rn;
    audio_resource_t *ar;
    mrp_attr_t a;
    size_t i;
    char disable[256];
    char requisite[1024];

    MRP_ASSERT(audio && buf && len > 0, "invalid argument");

    e = (p = buf) + len;
    *p = 0;
    
    if (zoneid < MRP_ZONE_MAX) {
        resources = audio->zones + zoneid;
        grantid = audio->grantids[zoneid];
    }
    else {
        resources = NULL;
        grantid = 0;
    }

    PRINT("      Resource '%s' - grantid:%u\n",
          MRP_SYSCTL_AUDIO_RESOURCE, grantid);

    if (!resources || mrp_list_empty(resources))
        PRINT("         No resources\n");
    else {
        mrp_list_foreach_back(resources, rentry, rn) {
            ar = mrp_list_entry(rentry, audio_resource_t, link);

            mrp_resmgr_disable_print(ar->disable, disable,
                                     sizeof(disable));
            mrp_application_requisite_print(ar->requisite, requisite,
                                            sizeof(requisite));

            PRINT("            "
                  "key:0x%08x %s %s grantid:%u requisite:%s disable:%s",
                  ar->key,
                  ar->interrupt ? "interrupt" : "base",
                  ar->acquire ? "acquire":"release",
                  ar->grantid,
                  requisite,
                  disable);

            for (i = 0;  i < MRP_ARRAY_SIZE(audio_attrs) - 1;  i++) {
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
        } /* mrp_list_foreach_back - resources */
    }

    return p - buf;
}


static int hash_compare(const void *key1, const void *key2)
{
    if (key1 < key2)
        return -1;
    if (key1 > key2)
        return 1;
    return 0;
}

static uint32_t hash_function(const void *key)
{
    return (uint32_t)(key - (const void *)0);
}

static const char *get_appid_for_resource(mrp_resource_t *res)
{
    mrp_attr_t attr;
    const char *appid;

    if (!mrp_resource_read_attribute(res, APPID_ATTRIDX, &attr) ||
        attr.type != mqi_string || !(appid = attr.value.string)  )
        appid = NULL;

    return appid;
}

static mrp_application_t *get_application_for_resource(mrp_resource_t *res)
{
    const char        *appid = get_appid_for_resource(res);
    mrp_application_t *app   = NULL;

    if (!appid || !(app = mrp_application_find(appid)))
        app = mrp_application_find(MRP_SYSCTL_APPID_DEFAULT);

    return app;
}

static uint32_t get_priority_for_resource(mrp_resource_t *res)
{
    mrp_attr_t attr;
    uint32_t priority = 0;

    if (mrp_resource_read_attribute(res, PRIORITY_ATTRIDX, &attr)) {
        if (attr.type == mqi_integer && attr.value.integer >= 0)
            priority = attr.value.integer;
    }

    return priority;
}

static uint32_t get_class_priority_for_resource(mrp_resource_t *res)
{
    mrp_attr_t attr;
    uint32_t priority = 0;

    if (mrp_resource_read_attribute(res, CLASSPRI_ATTRIDX, &attr)) {
        if (attr.type == mqi_integer && attr.value.integer >= 0)
            priority = attr.value.integer < 0 ? 0 : attr.value.integer;
    }

    return priority;
}

static audio_resource_t *audio_resource_create(mrp_resmgr_audio_t *audio,
                                               mrp_zone_t *zone,
                                               mrp_resource_t *res,
                                               mrp_application_class_t *ac)
{
    static uint32_t audioid;

    const char *zonename;
    const char *appid;
    const char *class_name;
    mrp_resmgr_t *resmgr;
    mrp_application_t *app;
    audio_resource_t *ar;
    void *hk;

    MRP_ASSERT(audio && zone && res && ac, "invalid argument");
    MRP_ASSERT(audio->resmgr, "confused with data structures");

    resmgr = audio->resmgr;
    zonename = mrp_zone_get_name(zone);
    appid = get_appid_for_resource(res);
    class_name = mrp_application_class_get_name(ac);
    ar = NULL;

    if (!(app = get_application_for_resource(res))) {
        mrp_log_error("system-controller: failed to create audio resource: "
                      "can't find app");
        return NULL;
    }
            
    if (!(ar = mrp_allocz(sizeof(*ar)))) {
        mrp_log_error("system-controller: failed to create audio resource: "
                      "can't allocate memory");
        return NULL;
    }

    mrp_list_init(&ar->link);
    ar->audio     = audio;
    ar->res       = res;
    ar->zoneid    = mrp_zone_get_id(zone);
    ar->audioid   = audioid++;
    ar->interrupt = strcmp(class_name, "player") && strcmp(class_name, "base");
    ar->key       = resource_key(ar);
    ar->requisite = app->requisites.audio;

    audio_insert_resource(ar);
                    
    mrp_debug("inserting resource to hash table: key=%p value=%p", res, ar);
    mrp_resmgr_insert_resource(resmgr, zone, res, ar);

    hk = NULL + ar->audioid;
    mrp_debug("inserting audio to hash table: key=%p value=%p", hk, ar);
    mrp_htbl_insert(audio->resources, hk, ar);

    mrp_resmgr_notifier_queue_audio_event(audio->resmgr, ar->zoneid, zonename,
                                          MRP_RESMGR_EVENTID_CREATE,
                                          app->appid, ar->audioid);
    mrp_resmgr_notifier_flush_audio_events(audio->resmgr, ar->zoneid);

    return ar;
}


static void audio_resource_destroy(mrp_resmgr_audio_t *audio,
                                    mrp_zone_t *zone,
                                    mrp_resource_t *res)
{
    audio_resource_t *ar;
    const char *zonename;
    const char *appid;

    MRP_ASSERT(audio && res && zone, "invalid argument");
    MRP_ASSERT(audio->resmgr, "confused with data structures");

    if ((ar = mrp_resmgr_remove_resource(audio->resmgr, zone, res))) {
        zonename = mrp_zone_get_name(zone);
        appid = get_appid_for_resource(res);

        mrp_resmgr_notifier_queue_audio_event(audio->resmgr,
                                              ar->zoneid, zonename,
                                              MRP_RESMGR_EVENTID_DESTROY,
                                              appid, ar->audioid);

        mrp_htbl_remove(audio->resources, NULL + ar->audioid, false);

        mrp_list_delete(&ar->link);
        mrp_free(ar);

        mrp_resmgr_notifier_flush_audio_events(audio->resmgr, ar->zoneid);
    }
}


static audio_resource_t *audio_resource_lookup(mrp_resmgr_audio_t *audio,
                                               mrp_resource_t *res)
{
    audio_resource_t *ar;

    MRP_ASSERT(audio && res, "invalid argument");
    MRP_ASSERT(audio->resmgr, "confused with data structures");

    ar = mrp_resmgr_lookup_resource(audio->resmgr, res);

    return ar;
}

static void audio_insert_resource(audio_resource_t *resource)
{
    mrp_resmgr_audio_t *audio;
    mrp_list_hook_t *resources, *insert_after, *n;
    audio_resource_t *ar;
    uint32_t key;

    mrp_list_delete(&resource->link);

    audio = resource->audio;
    resources = audio->zones + resource->zoneid;
    key = resource->key;
    insert_after = resources;   /* keep the compiler happy: foreach below
                                   will do it anyways */

    mrp_list_foreach_back(resources, insert_after, n) {
        ar = mrp_list_entry(insert_after, audio_resource_t, link);
        if (key >= ar->key)
            break;
    }

    mrp_list_insert_after(insert_after, &resource->link);
}

static void audio_grant_resources(mrp_resmgr_audio_t *audio,
                                  mrp_zone_t *zone)
{
    uint32_t zoneid;
    uint32_t grantid;
    const char *zonename;
    mrp_list_hook_t *resources, *rentry , *rn;
    audio_resource_t *ar;
    const char *appid;

    zoneid = mrp_zone_get_id(zone);
    resources = audio->zones + zoneid;
    grantid = ++(audio->grantids[zoneid]);

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";

    mrp_list_foreach_back(resources, rentry, rn) {
        ar = mrp_list_entry(rentry, audio_resource_t, link);

        MRP_ASSERT(ar->res, "confused with data structures");

        if (ar->acquire && !ar->disable) {
            if (!(appid = get_appid_for_resource(ar->res)))
                appid = "<unknown>";

            mrp_debug("preallocate audio resource for '%s' in zone '%s'",
                      appid, zonename);

            ar->grantid = grantid;

            if (!mrp_resource_is_shared(ar->res))
                break;
        }
    }
}

static void audio_queue_events(mrp_resmgr_audio_t *audio, mrp_zone_t *zone)
{
    uint32_t zoneid;
    const char *zonename;
    uint32_t grantid;
    mrp_list_hook_t *resources, *rentry, *rn;
    audio_resource_t *ar;
    bool grant;
    mrp_resmgr_eventid_t eventid;
    const char *appid;

    zoneid    = mrp_zone_get_id(zone);
    zonename  = mrp_zone_get_name(zone);
    resources = audio->zones + zoneid;
    grantid   = audio->grantids[zoneid];
    
    mrp_list_foreach_back(resources, rentry, rn) {
        ar = mrp_list_entry(rentry, audio_resource_t, link);
        grant = (grantid == ar->grantid);

        if ((grant && !ar->grant) || (!grant && ar->grant)) {
            eventid = grant ? MRP_RESMGR_EVENTID_GRANT :
                              MRP_RESMGR_EVENTID_REVOKE;
            appid = get_appid_for_resource(ar->res);

            mrp_resmgr_notifier_queue_audio_event(audio->resmgr,
                                                  zoneid, zonename,
                                                  eventid,
                                                  appid, ar->audioid);
        }

        ar->grant = grant;
    }
}


static uint32_t resource_key(audio_resource_t *ar)
{
    uint32_t stamp;
    uint32_t priority;
    uint32_t classpri;
    uint32_t acquire;
    uint32_t share;
    uint32_t interrupt;
    uint32_t key = 0;

    if (ar) {
        stamp     = 0; /* for now */
        priority  = get_priority_for_resource(ar->res);
        classpri  = get_class_priority_for_resource(ar->res);
        acquire   = ar->acquire ? 1 : 0;
        share     = mrp_resource_is_shared(ar->res) ? 1 : 0;
        interrupt = ar->interrupt ? 1 : 0;

        key = ((stamp     & STAMP_MASK    ) << STAMP_POSITION    ) |
              ((priority  & PRIORITY_MASK ) << PRIORITY_POSITION ) |
              ((classpri  & CLASSPRI_MASK ) << CLASSPRI_POSITION ) |
              ((acquire   & ACQUIRE_MASK  ) << ACQUIRE_POSITION  ) |
              ((share     & SHARE_MASK    ) << SHARE_POSITION    ) |
              ((interrupt & INTERRUPT_MASK) << INTERRUPT_POSITION) ;
    }

    return key;
}

static void audio_notify(mrp_resource_event_t event,
                          mrp_zone_t *zone,
                          mrp_application_class_t *ac,
                          mrp_resource_t *res,
                          void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    const char *zonename = mrp_zone_get_name(zone);
    audio_resource_t *ar;

    MRP_ASSERT(zone && ac && res && audio, "invalid argument");

    switch (event) {

    case MRP_RESOURCE_EVENT_CREATED:
        mrp_debug("audio resource in zone '%s' created", zonename);
        audio_resource_create(audio, zone, res, ac);
        break;

    case MRP_RESOURCE_EVENT_DESTROYED:
        mrp_debug("audio resource in zone '%s' destroyed", zonename);
        audio_resource_destroy(audio, zone, res);
        break;

    case MRP_RESOURCE_EVENT_ACQUIRE:
        mrp_debug("audio resource in zone '%s' is acquiring", zonename);
        if (!(ar = audio_resource_lookup(audio, res)))
            goto no_audio_resource;
        else
            if (!ar->acquire) {
                ar->acquire = true;
                ar->key = resource_key(ar);
                audio_insert_resource(ar);
            }
        break;

    case MRP_RESOURCE_EVENT_RELEASE:
        mrp_debug("audio resource in zone '%s' is released", zonename);
        if (!(ar = audio_resource_lookup(audio, res)))
            goto no_audio_resource;
        else
            if (ar->acquire) {
                ar->acquire = false;
                ar->key = resource_key(ar);
                audio_insert_resource(ar);
            }
        break;

    no_audio_resource:
        mrp_debug("resource lookup in hash table failed: key=%p", res);
        mrp_log_error("system-controller: can't find audio resource "
                      "in zone '%s'", zonename);
        break;

    default:
        mrp_log_error("system-controller: invalid event %d at audio "
                      "notification (zone '%s')", event, zonename);
        break;
    }
}

static void audio_init(mrp_zone_t *zone, void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    const char *zonename;

    MRP_ASSERT(zone && audio, "invalid argument");

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";

    mrp_debug("audio init in zone '%s'", zonename);

    audio_grant_resources(audio, zone);
}

static bool audio_allocate(mrp_zone_t *zone,
                            mrp_resource_t *res,
                            void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    uint32_t zoneid;
    const char *zonename;
    const char *appid = get_appid_for_resource(res);
    audio_resource_t *ar;
    uint32_t grantid;
    bool allocated;

    MRP_ASSERT(zone && res && audio && audio->resmgr, "invalid argument");

    zoneid  = mrp_zone_get_id(zone);
    grantid = audio->grantids[zoneid];

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";
    if (!(appid = get_appid_for_resource(res)))
        appid = "<unknown>";

    if ((ar = audio_resource_lookup(audio, res))) {
        allocated = (ar->grantid == grantid);

        mrp_debug("audio allocation for '%s' in zone '%s' %s",
                  zonename, appid, allocated ? "succeeded":"failed");

        return allocated;
    }

    mrp_log_error("system-controller: attempt to allocate untracked "
                  "resource '%s' in zone '%s'", appid, zonename);

    return FALSE;
}

static void audio_free(mrp_zone_t *zone, mrp_resource_t *res, void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    const char *zonename;
    const char *appid;
    audio_resource_t *ar;

    MRP_ASSERT(zone && res && audio, "invalid argument");

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";
    if (!(appid = get_appid_for_resource(res)))
        appid = "<unknown>";

    mrp_debug("free audio of '%s' in zone '%s'", appid, zonename);

    if ((ar = audio_resource_lookup(audio, res)))
        ar->grantid = 0;
}

static bool audio_advice(mrp_zone_t *zone,mrp_resource_t *res,void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    const char *zonename;
    const char *appid;

    MRP_ASSERT(zone && res && audio, "invalid argument");

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";
    if (!(appid = get_appid_for_resource(res)))
        appid = "<unknown>";

    mrp_debug("audio advice for '%s' in zone '%s'", appid, zonename);

    return TRUE;
}

static void audio_commit(mrp_zone_t *zone, void *userdata)
{
    mrp_resmgr_audio_t *audio = (mrp_resmgr_audio_t *)userdata;
    const char *zonename;
    uint32_t zoneid;

    MRP_ASSERT(zone && audio && audio->resmgr, "invalid argument");

    zoneid  = mrp_zone_get_id(zone);

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";

    mrp_debug("audio commit in zone '%s'", zonename);

    audio_queue_events(audio, zone);
    mrp_resmgr_notifier_flush_audio_events(audio->resmgr, zoneid);
}
