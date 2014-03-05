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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <murphy/common.h>
                                     /* this includes application.h */
#include "scripting-application.h"
#include "wayland/area.h"

static void init_applications(void);
static mrp_wayland_area_t *area_find(const char *);
static mrp_application_window_t *windows_dup(mrp_application_window_def_t *);

mrp_htbl_t *applications;

mrp_application_t *mrp_application_create(mrp_application_update_t *u,
                                          void *scripting_data)
{
#define IF_PRESENT(u,n) if ((u->mask & MRP_APPLICATION_ ## n ## _MASK))

    mrp_application_update_mask_t mask;
    mrp_application_t *app;
    mrp_wayland_area_t *area;
    mrp_wayland_t *wl;
    char buf[4096];
    void *it;

    MRP_ASSERT(u, "invalid argument");

    mask = u->mask;

    if (!(mask & MRP_APPLICATION_APPID_MASK) || !u->appid) {
        mrp_log_error("system-controller: failed to create application: "
                      "no appid");
        return NULL;
    }

    if (!(mask & MRP_APPLICATION_AREA_NAME_MASK) || !u->area_name) {
        mrp_log_error("system-controller: failed to create application '%s': "
                      "no area name", u->appid);
        return NULL;
    }

    if (!(app = mrp_allocz(sizeof(mrp_application_t)))) {
        mrp_log_error("system-controller: failed to create application '%s': "
                      "out of memory", u->appid);
        return NULL;
    }

    app->appid = mrp_strdup(u->appid);

    if (!applications) {
        init_applications();
        MRP_ASSERT(applications, "failed to initialize "
                   "hash table for applications");
    }

    if (!mrp_htbl_insert(applications, app->appid, app)) {
        mrp_log_error("failed to create application '%s': "
                      "already exists", app->appid);
        mrp_free(app);
        return NULL;
    }

    app->area_name = mrp_strdup(u->area_name);

    if ((area = area_find(app->area_name))) {
        mask |= MRP_APPLICATION_AREA_MASK;
        app->area = area;
    }

    IF_PRESENT(u, SCREEN_PRIVILEGE)
        app->privileges.screen = u->privileges.screen;
    IF_PRESENT(u, AUDIO_PRIVILEGE)
        app->privileges.audio = u->privileges.audio;
    IF_PRESENT(u, RESOURCE_CLASS)
        app->resource_class = mrp_strdup(u->resource_class);
    IF_PRESENT(u, SCREEN_PRIORITY)
        app->screen_priority = u->screen_priority;
    IF_PRESENT(u, SCREEN_REQUISITES)
        app->requisites.screen = u->requisites.screen;
    IF_PRESENT(u, AUDIO_REQUISITES)
        app->requisites.audio = u->requisites.audio;
    IF_PRESENT(u, WINDOWS)
        app->windows = windows_dup(u->windows);

    if (!scripting_data)
        scripting_data = mrp_application_scripting_app_create_from_c(app);

    app->scripting_data = scripting_data;

    mrp_application_print(app, mask, buf,sizeof(buf));
    mrp_debug("application '%s' created%s", app->appid, buf);

    if (app->scripting_data) {
        mrp_wayland_foreach(wl, it) {
            if (wl->application_update_callback) {
                wl->application_update_callback(wl, MRP_APPLICATION_CREATE,
                                                mask, app);
            }
        }
    }

    return app;

#undef IF_PRESENT
}

void mrp_application_destroy(mrp_application_t *app)
{
    mrp_wayland_t *wl;
    mrp_application_window_t *w;
    void *it;

    if (app && app->appid) {
        mrp_debug("destroying application '%s'", app->appid);

        mrp_wayland_foreach(wl, it) {
            if (wl->application_update_callback) {
                wl->application_update_callback(wl, MRP_APPLICATION_DESTROY,
                                                0, app);
            }
        }

        mrp_application_scripting_app_destroy_from_c(app);

        if ((void *)app != mrp_htbl_remove(applications, app->appid, false)) {
            mrp_log_error("failed to destroy application '%s': confused with "
                          "data structures", app->appid);
            return;
        }

        mrp_free(app->appid);

        if (app->windows) {
            for (w = app->windows;  w->window_name;   w++) {
                mrp_free((void *)w->window_name);
                mrp_free((void *)w->area_name);
            }
            mrp_free((void *)app->windows);
        }

        free(app);
    }
}


mrp_application_t *mrp_application_find(const char *appid)
{
    if (!appid)
        return NULL;

    return (mrp_application_t *)mrp_htbl_lookup(applications, (void *)appid);
}

mrp_wayland_area_t *mrp_application_area_find(mrp_application_t *app,
                                              const char *window_name)
{
    mrp_application_window_t *w;

    if (app) {
        if (window_name && app->windows) {
            for (w = app->windows;   w->window_name;   w++) {
                if (!strcmp(window_name, w->window_name))
                    return w->area;
            }
        }
        return app->area;
    }

    return NULL;
}

size_t mrp_application_print(mrp_application_t *app,
                             mrp_application_update_mask_t mask,
                             char *buf, size_t len)
{
#define PRINT(fmt, args...) \
    do{ if (p < e) { p += snprintf(p,e-p, "\n      " fmt, ## args); } }while(0)

    mrp_wayland_area_t *area;
    char *p, *e;
    char sbuf[1024];
    char abuf[1024];
    char wbuf[4096];

    e = (p = buf) + len;

    *p = 0;

    if ((mask & MRP_APPLICATION_AREA_NAME_MASK))
        PRINT("area_name: '%s'", app->area_name);
    if ((mask & MRP_APPLICATION_AREA_MASK)) {
        if (!(area = app->area))
            PRINT("area: -1 - <unknown>");
        else
            PRINT("area: %d - '%s'", area->areaid, area->name);
    }
    if ((mask & MRP_APPLICATION_PRIVILEGES_MASK)) {
        PRINT("privileges: screen=%s, audio=%s",
              mrp_application_privilege_str(app->privileges.screen),
              mrp_application_privilege_str(app->privileges.audio));
    }
    if ((mask & MRP_APPLICATION_RESOURCE_CLASS_MASK)) {
        PRINT("resource_class: '%s'",app->resource_class ? app->resource_class:
                                                           "<unknown>"       );
    }
    if ((mask & MRP_APPLICATION_SCREEN_PRIORITY_MASK)) {
        PRINT("screen_priority: %d", app->screen_priority);
    }
    if ((mask & MRP_APPLICATION_REQUISITES_MASK)) {
        mrp_application_requisite_print(app->requisites.screen,
                                        sbuf,sizeof(sbuf));
        mrp_application_requisite_print(app->requisites.audio,
                                        abuf,sizeof(abuf));
        PRINT("requisites: screen=%s, audio=%s", sbuf, abuf);
    }
    if ((mask & MRP_APPLICATION_WINDOWS_MASK)) {
        mrp_application_windows_print(app->windows, wbuf,sizeof(wbuf));
        PRINT("windows: %s", wbuf);
    }

    return p - buf;

#undef PRINT
}


void mrp_application_set_scripting_data(mrp_application_t *app, void *data)
{
    MRP_ASSERT(app, "Invalid Argument");


    mrp_debug("%sset scripting data", data ? "" : "re");

    app->scripting_data = data;
}

int mrp_application_foreach(mrp_htbl_iter_cb_t cb, void *user_data)
{
    return mrp_htbl_foreach(applications, cb, user_data);
}



const char *mrp_application_privilege_str(mrp_application_privilege_t priv)
{
    switch (priv) {
    case MRP_APPLICATION_PRIVILEGE_NONE:           return "none";
    case MRP_APPLICATION_PRIVILEGE_CERTIFIED:      return "certified";
    case MRP_APPLICATION_PRIVILEGE_MANUFACTURER:   return "manufacturer";
    case MRP_APPLICATION_PRIVILEGE_SYSTEM:         return "system";
    case MRP_APPLICATION_PRIVILEGE_UNLIMITED:      return "unlimited";
    default:                                       return "<unknown>";
    }
}

size_t mrp_application_requisite_print(mrp_application_requisite_t rqs,
                                       char *buf, size_t len)
{
#define PRINT(mask)                                                     \
    do {                                                                \
        if (p < e && (rqs & MRP_APPLICATION_REQUISITE_ ## mask)) {      \
            p += snprintf(p, e-p, "%s%s", p == q ? "":" | ", # mask);   \
        }                                                               \
    } while(0)

    char *p, *q, *e;

    e = (p = buf) + len;

    q = (p += snprintf(p, e-p, "0x%03x - ", (unsigned int)rqs));

    if (!rqs)
        p += snprintf(p, e-p, "none");
    else {
        PRINT(DRIVING);
        PRINT(PARKED);
        PRINT(REVERSES);
        PRINT(BLINKER_LEFT);
        PRINT(BLINKER_RIGHT);
    }

    return p - buf;

#undef PRINT
}


size_t mrp_application_windows_print(mrp_application_window_t *wins,
                                     char *buf, size_t len)
{
#define PRINT(w)                                                        \
    do {                                                                \
        if (p < e && w->window_name && w->area_name) {                  \
            p += snprintf(p, e-p, "%s%s:%s",                            \
                          p == buf ? "":", ",                           \
                          w->window_name,                               \
                          w->area ? w->area->name : w->area_name);      \
        }                                                               \
    } while(0)

    mrp_application_window_t *w;
    char *p, *e;

    e = (p = buf) + len;

    *p = 0;

    for (w = wins;   w && w->window_name;    w++)
        PRINT(w);

    if (p == buf)
        p += snprintf(p, e-p, "none");

    return p - buf;

#undef PRINT
}


static void init_applications(void)
{
    mrp_htbl_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.nentry = MRP_APPLICATION_MAX;
    cfg.comp = mrp_string_comp;
    cfg.hash = mrp_string_hash;
    cfg.nbucket = MRP_APPLICATION_BUCKETS;

    applications = mrp_htbl_create(&cfg);
}

static mrp_wayland_area_t *area_find(const char *fullname)
{
    mrp_wayland_t *wl;
    mrp_wayland_area_t *area;
    void *it;

    if (fullname) {
        mrp_wayland_foreach(wl, it) {
            if ((area = mrp_wayland_area_find(wl, fullname)))
                return area;
        }
    }

    return NULL;
}

static
mrp_application_window_t *windows_dup(mrp_application_window_def_t *defs)
{
    mrp_application_window_t *dup = NULL;
    const char *area_name;
    int i, n;

    if (defs) {

        for (n = 0;   defs[n].window_name;   n++)
            ;

        if ((dup = mrp_allocz(sizeof(mrp_application_window_t) * (n + 1)))) {
            for (i = 0;  i < n;  i++) {
                if ((area_name = defs[i].area_name)) {
                    dup[i].window_name = mrp_strdup(defs[i].window_name);
                    dup[i].area_name = mrp_strdup(area_name);
                    dup[i].area = area_find(area_name);
                }
            }
        }
    }

    return dup;
}
