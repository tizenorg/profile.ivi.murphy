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

#include "screen.h"
#include "notifier.h"
#include "wayland/area.h"
#include "wayland/output.h"
#include "application/application.h"

#define ANY_OUTPUT  (~((uint32_t)0))
#define ANY_AREA    (~((uint32_t)0))

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

#define PRIORITY_BITS         8
#define CLASSPRI_BITS         8
#define ZORDER_BITS           16

#define PRIORITY_POSITION     0
#define CLASSPRI_POSITION     (PRIORITY_POSITION + PRIORITY_BITS)
#define ZORDER_POSITION       (CLASSPRI_POSITION + CLASSPRI_BITS)

#define PRIORITY_MASK         MASK(PRIORITY_BITS)
#define CLASSPRI_MASK         MASK(CLASSPRI_BITS)
#define ZORDER_MASK           MASK(ZORDER_BITS)

#define PRIORITY_MAX          MAX(PRIORITY_BITS)
#define CLASSPRI_MAX          MAX(CLASSPRI_BITS)
#define ZORDER_MAX            MAX(ZORDER_BITS)


#define ATTRIBUTE(n,t,v)    {n, MRP_RESOURCE_RW, mqi_##t, {.t=v}}
#define ATTR_END            {NULL, 0, 0, {.string=NULL}}

typedef struct screen_resource_s   screen_resource_t;
typedef struct disable_iterator_s  disable_iterator_t;
typedef struct output_iterator_s   output_iterator_t;
typedef struct area_iterator_s     area_iterator_t;


struct screen_resource_s {
    mrp_list_hook_t link;
    mrp_resmgr_screen_t *screen;
    mrp_resource_t *res;
    uint32_t zoneid;
    size_t outputid;
    size_t areaid;
    uint32_t key;
    bool acquire;
    bool grant;
    uint32_t grantid;
    mrp_application_requisite_t requisite;
    mrp_resmgr_disable_t disable;
};

struct disable_iterator_s {
    uint32_t outputid;
    uint32_t areaid;
    bool disable;
    mrp_resmgr_disable_t type;
    uint32_t mask;
    union {
        const char *appid;
        mrp_application_requisite_t req;
        uint32_t surfaceid;
    };
    uint32_t zones;
    int counter;
};

struct output_iterator_s {
    const char *name;
    mrp_wayland_output_t *out;
};

struct area_iterator_s {
    mrp_resmgr_t *resmgr;
    const char *fullname;
    size_t areaid;
    size_t outputid;
    mrp_resmgr_screen_area_t *area;
    struct {
        uint32_t mask;
        const char *names[MRP_ZONE_MAX + 1];
    } zone;
    int counter;
};

static int hash_compare(const void *, const void *);
static uint32_t hash_function(const void *);

static void overlap_add(mrp_resmgr_screen_area_t *, size_t);
static void overlap_remove(mrp_resmgr_screen_area_t *, size_t);

static const char *get_appid_for_resource(mrp_resource_t *);
static int32_t get_surfaceid_for_resource(mrp_resource_t *);
static int32_t get_layerid_for_resource(mrp_resource_t *);
static const char *get_areaname_for_resource(mrp_resource_t *);
static mrp_application_t *get_application_for_resource(mrp_resource_t *);
static int32_t get_area_for_resource(mrp_resource_t *);
static uint32_t get_priority_for_resource(mrp_resource_t *);
static uint32_t get_class_priority_for_resource(mrp_resource_t *,
                                                mrp_application_class_t *);
static int32_t get_zone_id(const char *);


static screen_resource_t *screen_resource_create(mrp_resmgr_screen_t *,
                                                 mrp_zone_t *,mrp_resource_t *,
                                                 mrp_application_class_t *);
static void screen_resource_destroy(mrp_resmgr_screen_t *,  mrp_zone_t *,
                                    mrp_resource_t *);
static screen_resource_t *screen_resource_lookup(mrp_resmgr_screen_t *,
                                                 mrp_resource_t *);
static bool screen_resource_is_on_top(mrp_resmgr_screen_t *,
                                      screen_resource_t *);
static void screen_resource_raise_to_top(mrp_resmgr_screen_t *,
                                         screen_resource_t *);
static void screen_resource_lower_to_bottom(mrp_resmgr_screen_t *,
                                            screen_resource_t *);


static uint32_t zorder_new_top_value(mrp_resmgr_screen_area_t *);

static void screen_grant_resources(mrp_resmgr_screen_t *, mrp_zone_t *);
static void screen_queue_events(mrp_resmgr_screen_t *, mrp_zone_t *);

static void area_insert_resource(mrp_resmgr_screen_area_t*,screen_resource_t*);
static uint32_t resource_key(mrp_resource_t *, mrp_application_class_t *);

static void screen_notify(mrp_resource_event_t, mrp_zone_t *,
                          mrp_application_class_t *, mrp_resource_t *, void *);
static void screen_init(mrp_zone_t *, void *);
static bool screen_allocate(mrp_zone_t *, mrp_resource_t *, void *);
static void screen_free(mrp_zone_t *, mrp_resource_t *, void *);
static bool screen_advice(mrp_zone_t *, mrp_resource_t *, void *);
static void screen_commit(mrp_zone_t *, void *);

#define PRIORITY_ATTRIDX  0
#define CLASSPRI_ATTRIDX  1
#define AREA_ATTRIDX      2
#define APPID_ATTRIDX     3
#define SURFACE_ATTRIDX   4

static mrp_attr_def_t screen_attrs[] = {
    ATTRIBUTE("priority" , integer,       0      ),
    ATTRIBUTE("classpri" , integer,      -1      ),
    ATTRIBUTE("area"     , string , "<undefined>"),
    ATTRIBUTE("appid"    , string , "<undefined>"),
    ATTRIBUTE("surface"  , integer,       0      ),
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

mrp_resmgr_screen_t *mrp_resmgr_screen_create(mrp_resmgr_t *resmgr)
{
    mrp_resmgr_screen_t *screen;
    mrp_htbl_config_t cfg;
    uint32_t resid;
    size_t i;

    if ((screen = mrp_allocz(sizeof(mrp_resmgr_screen_t)))) {
        resid = mrp_resource_definition_create(MRP_SYSCTL_SCREEN_RESOURCE,
                                               true,screen_attrs,
                                               &screen_ftbl,screen);
        mrp_lua_resclass_create_from_c(resid);

        cfg.nentry = MRP_RESMGR_RESOURCE_MAX;
        cfg.comp = hash_compare;
        cfg.hash = hash_function;
        cfg.free = NULL;
        cfg.nbucket = MRP_RESMGR_RESOURCE_BUCKETS;

        screen->resmgr = resmgr;
        screen->resid = resid;
        screen->resources = mrp_htbl_create(&cfg);

        for (i = 0;  i < MRP_ZONE_MAX;  i++)
            mrp_list_init(screen->zones + i);
    }

    return screen;
}


void mrp_resmgr_screen_destroy(mrp_resmgr_screen_t *screen)
{
    if (screen) {
        mrp_htbl_destroy(screen->resources, false);
        mrp_free(screen);
    }
}

static int screen_disable_cb(void *key, void *object, void *user_data)
{
    screen_resource_t *sr = (screen_resource_t *)object;
    disable_iterator_t *it = (disable_iterator_t *)user_data;
    const char *appid;
    uint32_t disable;

    MRP_UNUSED(key);

    MRP_ASSERT(sr && it, "invalid argument");

    if ((it->outputid == ANY_OUTPUT || sr->outputid == it->outputid) &&
        (it->areaid   == ANY_AREA   || sr->areaid   == it->areaid   )  )
    {
        switch (it->type) {

        case MRP_RESMGR_DISABLE_REQUISITE:
            if (it->req && (it->req & sr->requisite) == it->req)
                goto disable;
            break;

        case MRP_RESMGR_DISABLE_APPID:
            if (it->appid) {
                if (!strcmp(it->appid, "*"))
                    goto disable;
                appid = get_appid_for_resource(sr->res);
                if (appid && !strcmp(it->appid, appid))
                    goto disable;
            }
            break;

        case MRP_RESMGR_DISABLE_SURFACEID:
            goto disable;

        disable:
            disable = sr->disable & it->mask;
            if (it->disable) {
                if (disable)
                    break;
                sr->disable |= it->mask;
            }
            else {
                if (!disable)
                    break;
                sr->disable &= ~it->mask;
            }
            it->counter++;
            it->zones |= (((uint32_t)1) << sr->zoneid);
            break;

        default:
            return MRP_HTBL_ITER_STOP;
        }
    }

    return MRP_HTBL_ITER_MORE;
}

static int output_find_cb(void *key, void *object, void *user_data)
{
    mrp_wayland_output_t *out = (mrp_wayland_output_t *)object;
    output_iterator_t *it = (output_iterator_t *)user_data;

    MRP_UNUSED(key);

    MRP_ASSERT(out && it, "invalid argument");

    if (out->name && !strcmp(it->name, out->outputname)) {
        it->out = out;
        return MRP_HTBL_ITER_STOP;
    }

    return MRP_HTBL_ITER_MORE;
}

int mrp_resmgr_screen_disable(mrp_resmgr_screen_t *screen,
                              const char *output_name,
                              const char *area_name,
                              bool disable,
                              mrp_resmgr_disable_t type,
                              void *data,
                              bool recalc_owner)
{
    mrp_wayland_t *w;
    disable_iterator_t dit;
    output_iterator_t oit;
    mrp_wayland_output_t *o;
    mrp_wayland_area_t *a;
    mrp_wayland_t *wl = NULL;
    uint32_t output_id = ANY_OUTPUT;
    uint32_t area_id = ANY_AREA;
    void *i;
    char fullname[1024];
    uint32_t mask;
    uint32_t z;
    void *hk, *obj;

    MRP_ASSERT(screen && data, "invalid argument");

    mrp_debug("output_name='%s' area_name='%s' %s, type=0x%02x data=%p",
              output_name ? output_name : "<any output>",
              area_name ? area_name : "<any area>",
              disable ? "disable" : "enable",
              type, data);

    if (output_name && strcmp(output_name, "*")) {
        memset(&oit, 0, sizeof(oit));
        oit.name = output_name;

        mrp_wayland_foreach(w, i) {
            mrp_htbl_foreach(w->outputs.by_index, output_find_cb, &oit);

            if ((o = oit.out)) {
                wl = w;
                output_id = o->outputid;
                break;
            }
        }
        if (output_id == ANY_OUTPUT) {
            mrp_log_error("system-controller: failed to disable screen: "
                          "can't find output '%s'", output_name);
            return -1;
        }
    }

    if (wl && area_name && strcmp(area_name, "*")) {
        snprintf(fullname, sizeof(fullname), "%s.%s", output_name, area_name);
        if ((a = mrp_wayland_area_find(wl, fullname)))
            area_id = a->areaid;
        else {
            mrp_log_error("system-controller: failed to disable screen: "
                          "can't find area '%s'", area_name);
            return -1;
        }
    }

    memset(&dit, 0, sizeof(dit));
    dit.outputid = output_id;
    dit.areaid = area_id;
    dit.disable = disable;
    dit.type = type;
    dit.zones = 0;
    dit.counter = 0;

    switch (type) {

    case MRP_RESMGR_DISABLE_REQUISITE:
        dit.mask = BIT(MRP_RESMGR_DISABLE_REQUISITE - 1);
        dit.req = *(mrp_application_requisite_t *)data;
        mrp_htbl_foreach(screen->resources, screen_disable_cb, &dit);
        break;

    case MRP_RESMGR_DISABLE_APPID:
        dit.mask = BIT(MRP_RESMGR_DISABLE_APPID - 1);
        dit.appid = (const char *)data;
        mrp_htbl_foreach(screen->resources, screen_disable_cb, &dit);
        break;

    case MRP_RESMGR_DISABLE_SURFACEID:
        dit.mask = BIT(MRP_RESMGR_DISABLE_APPID - 1);
        dit.surfaceid = *(uint32_t *)data;
        hk = NULL + dit.surfaceid;
        if (!(obj = mrp_htbl_lookup(screen->resources, hk))) {
            mrp_log_error("system-controller: failed to disable screen: "
                          "can't find surface %u", dit.surfaceid);
            return -1;
        }
        screen_disable_cb(hk, obj, &dit);
        break;

    default:
        mrp_log_error("system-controller: invalid type %d of "
                      "screen disable", type);
        return -1;
    }

    if (recalc_owner) {
        for (z = 0;   dit.zones && z < MRP_ZONE_MAX;   z++) {
            mask = (((uint32_t)1) << z);

            if ((mask & dit.zones)) {
                dit.zones &= ~mask;
                mrp_resource_owner_recalc(z);
            }
        }
    }

    return dit.counter;
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
    uint32_t grantid;
    mrp_resmgr_screen_area_t *area;
    mrp_list_hook_t *areas, *aentry, *an;
    mrp_list_hook_t *resources, *rentry, *rn;
    screen_resource_t *sr;
    mrp_attr_t a;
    size_t i;
    char disable[256];
    char requisite[1024];

    MRP_ASSERT(screen && buf && len > 0, "invalid argument");

    e = (p = buf) + len;
    *p = 0;

    if (zoneid < MRP_ZONE_MAX) {
        areas = screen->zones + zoneid;
        grantid = screen->grantids[zoneid];
    }
    else {
        areas = NULL;
        grantid = 0;
    }

    PRINT("      Resource '%s' - grantid:%u\n",
          MRP_SYSCTL_SCREEN_RESOURCE, grantid);

    if (!areas || mrp_list_empty(areas))
        PRINT("         No resources\n");
    else {
        mrp_list_foreach_back(areas, aentry, an) {
            area = mrp_list_entry(aentry, mrp_resmgr_screen_area_t, link);
            resources = &area->resources;

            PRINT("         Area '%s':\n", area->name);

            mrp_list_foreach_back(resources, rentry, rn) {
                sr = mrp_list_entry(rentry, screen_resource_t, link);

                mrp_resmgr_disable_print(sr->disable, disable,
                                         sizeof(disable));
                mrp_application_requisite_print(sr->requisite, requisite,
                                                sizeof(requisite));

                PRINT("            "
                      "key:0x%08x %s grantid:%u requisite:%s disable:%s",
                      sr->key,
                      sr->acquire ? "acquire":"release",
                      sr->grantid,
                      requisite,
                      disable);

                for (i = 0;  i < MRP_ARRAY_SIZE(screen_attrs) - 1;  i++) {
                    if ((mrp_resource_read_attribute(sr->res, i, &a))) {
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
        }  /* mrp_list_foreach_back - areas */
    }

    return p - buf;
}

static int area_resolution_cb(void *key, void *object, void *user_data)
{

    screen_resource_t  *sr = (screen_resource_t *)object;
    area_iterator_t *ait = (area_iterator_t *)user_data;
    const char *areaname;
    const char *appid;
    const char *zonename;
    int32_t surfaceid;
    int32_t layerid;

    MRP_UNUSED(key);

    MRP_ASSERT(sr && sr->res && ait, "confused with data structures");

    if (sr->areaid == ANY_AREA && sr->zoneid < MRP_ZONE_MAX) {
        areaname = get_areaname_for_resource(sr->res);

        if (areaname && !strcmp(areaname, ait->fullname)) {
            appid = get_appid_for_resource(sr->res);
            surfaceid = get_surfaceid_for_resource(sr->res);
            layerid = -1;

            mrp_debug("  resolving screen resource for '%s'",
                      appid ? appid : "<unknown appid>");

            sr->areaid = ait->areaid;
            sr->outputid = ait->outputid;

            area_insert_resource(ait->area, sr);

            ait->counter++;
            ait->zone.mask |= (((uint32_t)1) << sr->zoneid);

            if ((zonename = ait->zone.names[sr->zoneid])) {
                mrp_resmgr_notifier_queue_screen_event(ait->resmgr,
                                                    sr->zoneid, zonename,
                                                    MRP_RESMGR_EVENTID_CREATE,
                                                    appid, surfaceid, layerid,
                                                    ait->area->name);
                mrp_resmgr_notifier_flush_screen_events(ait->resmgr,
                                                    sr->zoneid);
            }
        }
    }

    return MRP_HTBL_ITER_MORE;
}

void mrp_resmgr_screen_area_create(mrp_resmgr_screen_t *screen,
                                   mrp_wayland_area_t *wlarea,
                                   const char *zonename)
{
    mrp_resmgr_screen_area_t *rmarea, *a;
    int32_t zoneid;
    size_t areaid;
    const char *name;
    int32_t outputid;
    size_t i;
    int32_t x, x0, x1, y, y0, y1;
    area_iterator_t ait;
    uint32_t mask;
    uint32_t z;

    MRP_ASSERT(screen && wlarea && wlarea->output && zonename,
               "invalid argument");

    if (wlarea->areaid < 0 || wlarea->areaid >= MRP_WAYLAND_AREA_MAX) {
        mrp_log_error("system-controller: refuse to create screen area '%s': "
                      "id %d is out of range (0 - %d)",
                      wlarea->name, wlarea->areaid, MRP_WAYLAND_AREA_MAX - 1);
        return;
    }

    areaid   = wlarea->areaid;
    name     = wlarea->name ? wlarea->name : "<unknown>";
    outputid = wlarea->output->outputid;

    if ((zoneid = get_zone_id(zonename))) {
        mrp_log_error("system-controller: can't create resource manager area "
                      "%d: can't find zone '%s' for it", areaid, zonename);
        return;
    }

    if (areaid >= screen->narea) {
        screen->areas = mrp_reallocz(screen->areas, screen->narea, areaid + 1);

        MRP_ASSERT(screen->areas, "can't allocate memory for screen areas");

        screen->narea = areaid + 1;
    }

    if (screen->areas[areaid]) {
        mrp_log_error("system-controller: attempt to redefine "
                      "resource manager area %d", areaid);
        return;
    }

    screen->areas[areaid] = rmarea = mrp_allocz(sizeof(*rmarea));

    mrp_list_append(screen->zones + zoneid, &rmarea->link);
    rmarea->name = mrp_strdup(name);
    rmarea->outputid = outputid;
    rmarea->x = wlarea->x;
    rmarea->y = wlarea->y;
    rmarea->width = wlarea->width;
    rmarea->height = wlarea->height;
    mrp_list_init(&rmarea->resources);

    x1 = (x0 = rmarea->x) + rmarea->width;
    y1 = (y0 = rmarea->y) + rmarea->height;

    for (i = 0;  i < screen->narea; i++) {
        if ((a = screen->areas[i]) && i != areaid) {
            if ((      a->outputid == outputid        ) &&
                (((x  = a->x     ) >= x0 && x < x1) ||
                 ((x += a->width ) >= x0 && x < x1) ||
                 ((y  = a->y     ) >= y0 && y < y1) ||
                 ((y += a->height) >= y0 && y < y1)   )   )
            {
                overlap_add(a, areaid);
                overlap_add(rmarea, i);
            }
        }
    }

    mrp_debug("resolving resources in '%s' area", wlarea->fullname);

    memset(&ait, 0, sizeof(ait));
    ait.resmgr   = screen->resmgr;
    ait.fullname = wlarea->fullname;
    ait.areaid   = areaid;
    ait.outputid = outputid;
    ait.area     = rmarea;
    mrp_zone_get_all_names(MRP_ZONE_MAX + 1, ait.zone.names);

    mrp_htbl_foreach(screen->resources, area_resolution_cb, &ait);

    if (ait.zone.mask) {
        mrp_debug("recalculating owners ...");

        for (z = 0;   ait.zone.mask && z < MRP_ZONE_MAX;   z++) {
            mask = (((uint32_t)1) << z);

            if ((mask & ait.zone.mask)) {
                ait.zone.mask &= ~mask;
                mrp_resource_owner_recalc(z);
            }
        }
    }

    mrp_log_info("system-controller: resource manager registered screen area "
                 "%d - '%s'", areaid, name);
}

void mrp_screen_area_destroy(mrp_resmgr_screen_t *screen, int32_t areaid)
{
    mrp_resmgr_screen_area_t *area;
    size_t i;

    MRP_ASSERT(screen && areaid >= 0 && areaid < MRP_WAYLAND_AREA_MAX,
               "invalid argument");

    if (areaid >= (int32_t)screen->narea || !(area = screen->areas[areaid])) {
        mrp_log_error("system-controller: attempt to destroy non-existent "
                      "resource manager area %d", areaid);
    }
    else {
        screen->areas[areaid] = NULL;

        for (i = 0;  i < area->noverlap;  i++)
            overlap_remove(screen->areas[area->overlaps[i]], areaid);

        mrp_free((void *)area->name);
        mrp_free((void *)area->overlaps);
        mrp_free(area);
    }
}

void mrp_screen_resource_raise(mrp_resmgr_screen_t *screen,
                               const char *appid,
                               int32_t surfaceid)
{
    mrp_resmgr_screen_area_t *area;
    mrp_list_hook_t *resources, *entry, *n;
    screen_resource_t *sr;
    mrp_resource_t *res;
    const char *id;
    size_t i, cnt;
    size_t zmax, zmin;
    bool zones[MRP_ZONE_MAX];

    MRP_ASSERT(screen && screen->resources && appid, "invalid argument");

    if (surfaceid == 0) {
        cnt = 0;
        zmax = 0;
        zmin = MRP_ZONE_MAX-1;

        for (i = cnt = 0;  i < screen->narea;  i++) {
            if (!(area = screen->areas[i]))
                continue;

            resources = &area->resources;

            mrp_list_foreach(resources, entry, n) {
                sr = mrp_list_entry(entry, screen_resource_t, link);
                res = sr->res;

                if ((id = get_appid_for_resource(res)) && !strcmp(id, appid)) {
                    mrp_debug("raise surface %d to top", surfaceid);

                    if (!screen_resource_is_on_top(screen, sr)) {
                        screen_resource_raise_to_top(screen, sr);

                        cnt++;
                        zones[sr->zoneid] = true;
                        if (zmax < sr->zoneid)  zmax = sr->zoneid;
                        if (zmin > sr->zoneid)  zmin = sr->zoneid;
                    }
                    break;
                }
            }
        }

        if (!cnt)
            mrp_debug("nothing to be raised");
        else {
            for (i = zmin;  i <= zmax;  i++) {
                if (zones[i])
                    mrp_resource_owner_recalc(i);
            }
        }
    }
    else {
        if ((sr = mrp_htbl_lookup(screen->resources, NULL + surfaceid))) {
            res = sr->res;

            if (!(id = get_appid_for_resource(res)) || strcmp(id, appid)) {
                mrp_log_error("system-controller: can't raise window %u: "
                              "appid mismatch ('%s' vs. '%s')",
                              surfaceid, id, appid);
            }
            else {
                if (screen_resource_is_on_top(screen, sr)) {
                    mrp_debug("nothing to be raised: surface %d "
                              "is already on top", surfaceid);
                }
                else {
                    mrp_debug("raise surface %d to top", surfaceid);
                    screen_resource_raise_to_top(screen, sr);
                    mrp_resource_owner_recalc(sr->zoneid);
                }
            }
        }
    }
}

void mrp_screen_resource_lower(mrp_resmgr_screen_t *screen,
                               const char *appid,
                               int32_t surfaceid)
{
    mrp_resmgr_screen_area_t *area;
    mrp_list_hook_t *resources, *entry, *n;
    screen_resource_t *sr;
    mrp_resource_t *res;
    const char *id;
    size_t i, cnt;
    size_t zmax, zmin;
    bool zones[MRP_ZONE_MAX];

    MRP_ASSERT(screen && screen->resources && appid, "invalid argument");

    if (surfaceid == 0) {
        cnt = 0;
        zmax = 0;
        zmin = MRP_ZONE_MAX-1;

        for (i = cnt = 0;  i < screen->narea;  i++) {
            if (!(area = screen->areas[i]))
                continue;

            resources = &area->resources;

            mrp_list_foreach_back(resources, entry, n) {
                sr = mrp_list_entry(entry, screen_resource_t, link);
                res = sr->res;

                if ((id = get_appid_for_resource(res)) && !strcmp(id, appid)) {
                    mrp_debug("lower surface %d to bottom", surfaceid);

                    screen_resource_lower_to_bottom(screen, sr);

                    cnt++;
                    zones[sr->zoneid] = true;
                    if (zmax < sr->zoneid)  zmax = sr->zoneid;
                    if (zmin > sr->zoneid)  zmin = sr->zoneid;
                }
            }
        }

        if (!cnt)
            mrp_debug("nothing to be lowered");
        else {
            for (i = zmin;  i <= zmax;  i++) {
                if (zones[i])
                    mrp_resource_owner_recalc(i);
            }
        }
    }
    else {
        if ((sr = mrp_htbl_lookup(screen->resources, NULL + surfaceid))) {
            res = sr->res;

            if (!(id = get_appid_for_resource(res)) || strcmp(id, appid)) {
                mrp_log_error("system-controller: can't lower window %u: "
                              "appid mismatch ('%s' vs. '%s')",
                              surfaceid, id, appid);
            }
            else {
                mrp_debug("lower surface %d to bottom", surfaceid);
                screen_resource_lower_to_bottom(screen, sr);
                mrp_resource_owner_recalc(sr->zoneid);
            }
        }
    }
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

static void overlap_add(mrp_resmgr_screen_area_t *area, size_t overlapid)
{
    size_t i;

    for (i = 0;  i < area->noverlap;  i++) {
        if (area->overlaps[i] == overlapid)
            return;
    }

    area->overlaps = mrp_realloc(area->overlaps, sizeof(size_t *) * (i + 1));

    MRP_ASSERT(area->overlaps, "can't allocate memory for overalapping "
               "resource manager areas");

    area->overlaps[i] = overlapid;
    area->noverlap++;
}

static void overlap_remove(mrp_resmgr_screen_area_t *area, size_t overlapid)
{
    size_t i;

    for (i = 0;  i < area->noverlap;  i++) {
        if (area->overlaps[i] == overlapid) {
            for (i++; i < area->noverlap; i++)
                area->overlaps[i-1] = area->overlaps[i];
            area->noverlap--;
            return;
        }
    }

    mrp_log_error("system-controller: attempt to remove unregistered "
                  "overlapping area");
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

static int32_t get_surfaceid_for_resource(mrp_resource_t *res)
{
    mrp_attr_t attr;

    if (mrp_resource_read_attribute(res, SURFACE_ATTRIDX, &attr)) {
        if (attr.type == mqi_integer)
            return attr.value.integer;
    }

    return 0;
}

static int32_t get_layerid_for_resource(mrp_resource_t *res)
{
    MRP_UNUSED(res);

    return -1;
}

static const char *get_areaname_for_resource(mrp_resource_t *res)
{
    mrp_attr_t attr;

    if (mrp_resource_read_attribute(res, AREA_ATTRIDX, &attr)) {
        if (attr.type == mqi_string)
            return attr.value.string;
    }

    return NULL;
}

static mrp_application_t *get_application_for_resource(mrp_resource_t *res)
{
    const char        *appid = get_appid_for_resource(res);
    mrp_application_t *app   = NULL;

    if (!appid || !(app = mrp_application_find(appid)))
        app = mrp_application_find(MRP_SYSCTL_APPID_DEFAULT);

    return app;
}

static int32_t get_area_for_resource(mrp_resource_t *res)
{
    mrp_wayland_t *wl;
    mrp_wayland_area_t *a;
    const char *areaname;
    void *it;

    if ((areaname = get_areaname_for_resource(res))) {
        mrp_wayland_foreach(wl, it) {
            if ((a = mrp_wayland_area_find(wl, areaname)))
                return a->areaid;
        }
    }

    return -1;
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

static uint32_t get_class_priority_for_resource(mrp_resource_t *res,
                                                mrp_application_class_t *ac)
{
    mrp_attr_t attr;
    uint32_t priority;

    priority = mrp_application_class_get_priority(ac);

    if (mrp_resource_read_attribute(res, CLASSPRI_ATTRIDX, &attr)) {
        if (attr.type == mqi_integer && attr.value.integer >= 0)
            priority = attr.value.integer;
    }

    return priority;
}

static int32_t get_zone_id(const char *name)
{
    const char *zones[MRP_ZONE_MAX + 1];
    const char *zone;
    int32_t id;

    if (mrp_zone_get_all_names(MRP_ZONE_MAX + 1, zones)) {
        for (id = 0;   (zone = zones[id]);  id++) {
            if (!strcmp(name, zone))
                return id;
        }
    }

    return -1;
}



static screen_resource_t *screen_resource_create(mrp_resmgr_screen_t *screen,
                                                 mrp_zone_t *zone,
                                                 mrp_resource_t *res,
                                                 mrp_application_class_t *ac)
{
    mrp_resmgr_t *resmgr;
    const char *zonename;
    const char *appid;
    mrp_application_t *app;
    mrp_resmgr_screen_area_t *area;
    int32_t id;
    size_t layerid;
    size_t areaid;
    int32_t outputid;
    int32_t surfaceid;
    screen_resource_t *sr;
    void *hk;

    MRP_ASSERT(screen && screen->resources && zone && res && ac,
               "invalid argument");
    MRP_ASSERT(screen->resmgr, "confused with data structures");

    resmgr = screen->resmgr;
    sr = NULL;

    zonename = mrp_zone_get_name(zone);
    appid = get_appid_for_resource(res);

    if (!(app = get_application_for_resource(res))) {
        mrp_log_error("system-controller: failed to create screen resource: "
                      "can't find app");
        return NULL;
    }

    layerid = get_layerid_for_resource(sr->res);

    if ((id = get_area_for_resource(res)) >= 0 &&
        (size_t)id < screen->narea             &&
        (area = screen->areas[id])              )
    {
        areaid = id;
        outputid = area->outputid;
    }
    else {
        mrp_debug("delayed area resolution");
        area = NULL;
        areaid = ANY_AREA;
        outputid = ANY_OUTPUT;
    }

    if (!(surfaceid = get_surfaceid_for_resource(res))) {
        mrp_log_error("system-controller: failed to create screen resource: "
                      "invalid surface attribute");
        return NULL;
    }

    if (!(sr = mrp_allocz(sizeof(*sr)))) {
        mrp_log_error("system-controller: failed to create screen resource: "
                      "can't allocate memory");
        return NULL;
    }

    mrp_list_init(&sr->link);
    sr->screen    = screen;
    sr->res       = res;
    sr->zoneid    = mrp_zone_get_id(zone);
    sr->outputid  = outputid;
    sr->areaid    = areaid;
    sr->key       = resource_key(res, ac);
    sr->requisite = app->requisites.screen;

    if (area)
        area_insert_resource(area, sr);

    mrp_debug("inserting resource to hash table: key=%p value=%p", res, sr);
    mrp_resmgr_insert_resource(resmgr, zone, res, sr);

    hk = NULL + surfaceid;
    mrp_debug("inserting surface to hash table: key=%p value=%p", hk, sr);
    mrp_htbl_insert(screen->resources, hk, sr);

    if (area) {
        mrp_resmgr_notifier_queue_screen_event(screen->resmgr,
                                               sr->zoneid,zonename,
                                               MRP_RESMGR_EVENTID_CREATE,
                                               appid, surfaceid, layerid,
                                               area->name);
        mrp_resmgr_notifier_flush_screen_events(screen->resmgr, sr->zoneid);
    }

    return sr;
}


static void screen_resource_destroy(mrp_resmgr_screen_t *screen,
                                    mrp_zone_t *zone,
                                    mrp_resource_t *res)
{
    screen_resource_t *sr;
    const char *zonename;
    const char *appid;
    int32_t layerid;
    int32_t surfaceid;
    const char *areaname;

    MRP_ASSERT(res && screen && screen->resources, "invalid argument");
    MRP_ASSERT(screen->resmgr, "confused with data structures");

    if ((sr = mrp_resmgr_remove_resource(screen->resmgr, zone, res))) {
        zonename  = mrp_zone_get_name(zone);
        appid     = get_appid_for_resource(res);
        surfaceid = get_surfaceid_for_resource(res);
        layerid   = get_layerid_for_resource(res);
        areaname  = get_areaname_for_resource(res);

        mrp_resmgr_notifier_queue_screen_event(screen->resmgr,
                                               sr->zoneid, zonename,
                                               MRP_RESMGR_EVENTID_DESTROY,
                                               appid, surfaceid, layerid,
                                               areaname);
        if (surfaceid)
            mrp_htbl_remove(screen->resources, NULL + surfaceid, false);

        mrp_list_delete(&sr->link);
        mrp_free(sr);

        mrp_resmgr_notifier_flush_screen_events(screen->resmgr, sr->zoneid);
    }
}


static screen_resource_t *screen_resource_lookup(mrp_resmgr_screen_t *screen,
                                                 mrp_resource_t *res)
{
    screen_resource_t *sr;

    MRP_ASSERT(res && screen, "invalid argument");
    MRP_ASSERT(screen->resmgr, "confused with data structures");

    sr = mrp_resmgr_lookup_resource(screen->resmgr, res);

    return sr;
}

static bool screen_resource_is_on_top(mrp_resmgr_screen_t *screen,
                                      screen_resource_t *sr)
{
    mrp_resmgr_screen_area_t *area;
    bool on_top;

    if (sr->areaid >= screen->narea || !(area = screen->areas[sr->areaid]))
        on_top = false;
    else
        on_top = (area->resources.prev == &sr->link);

    return on_top;
}

static void screen_resource_raise_to_top(mrp_resmgr_screen_t *screen,
                                         screen_resource_t *sr)
{
    mrp_resmgr_screen_area_t *area;

    if (sr->areaid >= screen->narea || !(area = screen->areas[sr->areaid])) {
        mrp_log_error("system-controller: failed to raise screen resource: "
                      "can't find area for screen");
    }
    else {
        sr->key &= ~(ZORDER_MASK << ZORDER_POSITION);
        sr->key |= zorder_new_top_value(area);

        area_insert_resource(area, sr);
    }

    sr->acquire = true;
}


static void screen_resource_lower_to_bottom(mrp_resmgr_screen_t *screen,
                                            screen_resource_t *sr)
{
    mrp_resmgr_screen_area_t *area;

    if (sr->areaid >= screen->narea || !(area = screen->areas[sr->areaid])) {
        mrp_log_error("system-controller: failed to lower screen resource: "
                      "can't find area for screen");
    }
    else {
        sr->key &= ~(ZORDER_MASK << ZORDER_POSITION);

        area_insert_resource(area, sr);
    }

    sr->acquire = false;
}


static uint32_t zorder_new_top_value(mrp_resmgr_screen_area_t *area)
{
    mrp_list_hook_t *resources, *entry, *n;
    screen_resource_t *sr;
    uint32_t new_top, min, max, z;

    if ((new_top = ++(area->zorder)) >= ZORDER_MAX) {
        /*
         * we get here in the unlikely event of z-order value overflow
         * what we try to do here is to find out the range of the z values
         * and subtract from all the minimum value, ie. move the whole range
         * to start from zero.
         */
        resources = &area->resources;

        if (mrp_list_empty(resources))
            new_top = area->zorder = 1;
        else {
            min = ZORDER_MAX;
            max = 0;

            mrp_list_foreach(resources, entry, n) {
                sr = mrp_list_entry(entry, screen_resource_t, link);
                z = ((sr->key >> ZORDER_POSITION) & ZORDER_MASK);

                if (z < min)   min = z;
                if (z > max)   max = z;
            }

            /* assert if the range moving would not help */
            MRP_ASSERT(min < ZORDER_MAX-1, "Z-order overflow");

            mrp_list_foreach(resources, entry, n) {
                sr = mrp_list_entry(entry, screen_resource_t, link);
                z = ((sr->key >> ZORDER_POSITION) & ZORDER_MASK) - min;

                sr->key &= ~(ZORDER_MASK << ZORDER_POSITION);
                sr->key |= (z << ZORDER_POSITION);
            }

            new_top = area->zorder = (max - min) + 1;
        }
    }

    return (new_top << ZORDER_POSITION);
}


static void screen_grant_resources(mrp_resmgr_screen_t *screen,
                                   mrp_zone_t *zone)
{
    uint32_t zoneid;
    uint32_t grantid;
    const char *zonename;
    mrp_list_hook_t *areas, *aentry, *an;
    mrp_list_hook_t *resources, *rentry , *rn;
    mrp_resmgr_screen_area_t *area;
    screen_resource_t *sr;
    const char *appid;
    int32_t surfaceid;
    int32_t layerid;

    zoneid   = mrp_zone_get_id(zone);
    zonename = mrp_zone_get_name(zone);
    areas    = screen->zones + zoneid;
    grantid  = ++(screen->grantids[zoneid]);

    if (!zonename)
        zonename = "<unknown>";

    mrp_list_foreach(areas, aentry, an) {
        area = mrp_list_entry(aentry, mrp_resmgr_screen_area_t, link);
        resources = &area->resources;

        mrp_list_foreach_back(resources, rentry, rn) {
            sr = mrp_list_entry(rentry, screen_resource_t, link);

            MRP_ASSERT(sr->res, "confused with data structures");

            if (sr->acquire && !sr->disable) {
                appid     = get_appid_for_resource(sr->res);
                surfaceid = get_surfaceid_for_resource(sr->res);
                layerid   = get_layerid_for_resource(sr->res);

                if (!appid)
                    appid = "<unknown>";

                mrp_debug("preallocate screen resource in '%s' area for '%s' "
                          "in zone '%s'", area->name, appid, zonename);

                sr->grantid = grantid;

                mrp_resmgr_notifier_queue_screen_event(screen->resmgr,
                                                zoneid, zonename,
                                                MRP_RESMGR_EVENTID_PREALLOCATE,
                                                appid, surfaceid, layerid,
                                                area->name);
                break;
            }
        }
    }
}

static void screen_queue_events(mrp_resmgr_screen_t *screen, mrp_zone_t *zone)
{
    uint32_t zoneid;
    const char *zonename;
    uint32_t grantid;
    mrp_list_hook_t *areas, *aentry, *an;
    mrp_list_hook_t *resources, *rentry, *rn;
    mrp_resmgr_screen_area_t *area;
    screen_resource_t *sr;
    bool grant;
    mrp_resmgr_eventid_t eventid;
    const char *appid, *areaname;
    int32_t surfaceid, layerid;

    zoneid   = mrp_zone_get_id(zone);
    zonename = mrp_zone_get_name(zone);
    areas    = screen->zones + zoneid;
    grantid  = screen->grantids[zoneid];

    mrp_list_foreach(areas, aentry, an) {
        area = mrp_list_entry(aentry, mrp_resmgr_screen_area_t, link);
        resources = &area->resources;


        mrp_list_foreach_back(resources, rentry, rn) {
            sr = mrp_list_entry(rentry, screen_resource_t, link);
            grant = (grantid == sr->grantid);

            if ((grant && !sr->grant) || (!grant && sr->grant)) {

                eventid   = grant ? MRP_RESMGR_EVENTID_GRANT :
                                    MRP_RESMGR_EVENTID_REVOKE;
                appid     = get_appid_for_resource(sr->res);
                surfaceid = get_surfaceid_for_resource(sr->res);
                layerid   = get_layerid_for_resource(sr->res);
                areaname  = get_areaname_for_resource(sr->res);

                mrp_resmgr_notifier_queue_screen_event(screen->resmgr,
                                                       zoneid, zonename,
                                                       eventid,
                                                       appid, surfaceid,
                                                       layerid, areaname);
            }

            sr->grant = grant;
        }
    }
}


static void area_insert_resource(mrp_resmgr_screen_area_t *area,
                                 screen_resource_t *resource)
{
    mrp_list_hook_t *resources, *insert_after, *n;
    screen_resource_t *sr;
    uint32_t key;

    mrp_list_delete(&resource->link);

    resources = &area->resources;
    key = resource->key;
    insert_after = resources;   /* keep the compiler happy: foreach below
                                   will do it anyways */

    mrp_list_foreach_back(resources, insert_after, n) {
        sr = mrp_list_entry(insert_after, screen_resource_t, link);
        if (key >= sr->key)
            break;
    }

    mrp_list_insert_after(insert_after, &resource->link);
}

static uint32_t resource_key(mrp_resource_t *res, mrp_application_class_t *ac)
{
    uint32_t priority;
    uint32_t classpri;
    uint32_t key = 0;

    if (res && ac) {
        priority = get_priority_for_resource(res);
        classpri = get_class_priority_for_resource(res, ac);
        key = ((priority & PRIORITY_MASK) << PRIORITY_POSITION) |
              ((classpri & CLASSPRI_MASK) << CLASSPRI_POSITION) ;
    }

    return key;
}

static void screen_notify(mrp_resource_event_t event,
                          mrp_zone_t *zone,
                          mrp_application_class_t *ac,
                          mrp_resource_t *res,
                          void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zonename = mrp_zone_get_name(zone);
    screen_resource_t *sr;

    MRP_ASSERT(zone && ac && res && screen, "invalid argument");

    switch (event) {

    case MRP_RESOURCE_EVENT_CREATED:
        mrp_debug("screen resource in zone '%s' created", zonename);
        screen_resource_create(screen, zone, res, ac);
        break;

    case MRP_RESOURCE_EVENT_DESTROYED:
        mrp_debug("screen resource in zone '%s' destroyed", zonename);
        screen_resource_destroy(screen, zone, res);
        break;

    case MRP_RESOURCE_EVENT_ACQUIRE:
        mrp_debug("screen resource in zone '%s' is acquiring", zonename);
        if (!(sr = screen_resource_lookup(screen, res)))
            goto no_screen_resource;
        else
            screen_resource_raise_to_top(screen, sr);
        break;

    case MRP_RESOURCE_EVENT_RELEASE:
        mrp_debug("screen resource in zone '%s' is released", zonename);
        if (!(sr = screen_resource_lookup(screen, res)))
            goto no_screen_resource;
        else
            screen_resource_lower_to_bottom(screen, sr);
        break;

    no_screen_resource:
        mrp_debug("resource lookup in hash table failed: key=%p", res);
        mrp_log_error("system-controller: can't find screen resource "
                      "in zone '%s'", zonename);
        break;

    default:
        mrp_log_error("system-controller: invalid event %d at screen "
                      "notification (zone '%s')", event, zonename);
        break;
    }
}

static void screen_init(mrp_zone_t *zone, void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    uint32_t zoneid;
    const char *zonename;

    MRP_ASSERT(zone && screen, "invalid argument");

    zoneid   = mrp_zone_get_id(zone);
    zonename = mrp_zone_get_name(zone);

    if (!zonename)
        zonename = "<unknown>";

    mrp_debug("screen init in zone '%s'", zonename);

    mrp_resmgr_notifier_queue_screen_event(screen->resmgr,
                                           zoneid,zonename,
                                           MRP_RESMGR_EVENTID_INIT,
                                           "<unknown>", -1, -1, "<unknown>");
    screen_grant_resources(screen, zone);

    mrp_resmgr_notifier_flush_screen_events(screen->resmgr, zoneid);
}

static bool screen_allocate(mrp_zone_t *zone,
                            mrp_resource_t *res,
                            void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    uint32_t zoneid;
    const char *zonename;
    const char *appid = get_appid_for_resource(res);
    screen_resource_t *sr;
    uint32_t grantid;
    bool allocated;

    MRP_ASSERT(zone && res && screen && screen->resmgr, "invalid argument");

    zoneid  = mrp_zone_get_id(zone);
    grantid = screen->grantids[zoneid];

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";
    if (!(appid = get_appid_for_resource(res)))
        appid = "<unknown>";

    if ((sr = screen_resource_lookup(screen, res))) {
        allocated = (sr->grantid == grantid);

        mrp_debug("screen allocation for '%s' in zone '%s' %s",
                  zonename, appid, allocated ? "succeeded":"failed");

        return allocated;
    }

    mrp_log_error("system-controller: attempt to allocate untracked "
                  "resource '%s' in zone '%s'", appid, zonename);

    return FALSE;
}

static void screen_free(mrp_zone_t *zone, mrp_resource_t *res, void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zonename;
    const char *appid;
    screen_resource_t *sr;

    MRP_ASSERT(zone && res && screen, "invalid argument");

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";
    if (!(appid = get_appid_for_resource(res)))
        appid = "<unknown>";

    mrp_debug("free screen of '%s' in zone '%s'", appid, zonename);

    if ((sr = screen_resource_lookup(screen, res)))
        sr->grantid = 0;
}

static bool screen_advice(mrp_zone_t *zone,mrp_resource_t *res,void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zonename;
    const char *appid;

    MRP_ASSERT(zone && res && screen, "invalid argument");

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";
    if (!(appid = get_appid_for_resource(res)))
        appid = "<unknown>";

    mrp_debug("screen advice for '%s' in zone '%s'", appid, zonename);

    return TRUE;
}

static void screen_commit(mrp_zone_t *zone, void *userdata)
{
    mrp_resmgr_screen_t *screen = (mrp_resmgr_screen_t *)userdata;
    const char *zonename;
    uint32_t zoneid;

    MRP_ASSERT(zone && screen && screen->resmgr, "invalid argument");

    zoneid  = mrp_zone_get_id(zone);

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";

    mrp_debug("screen commit in zone '%s'", zonename);

    screen_queue_events(screen, zone);
    mrp_resmgr_notifier_flush_screen_events(screen->resmgr, zoneid);
}
