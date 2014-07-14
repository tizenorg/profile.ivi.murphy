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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <murphy/common.h>

#include <genivi-shell/ivi-controller-client-protocol.h>
#include <genivi-shell/ivi-application-client-protocol.h>

#include "glm-window-manager.h"
#include "layer.h"
#include "output.h"
#include "animation.h"
#include "window.h"
#include "window-manager.h"
#include "area.h"

#define MAX_COORDINATE                  16383
#define CONSTRUCTOR_TIMEOUT             2000 /* ms */

#define INVALID_INDEX                   (~(uint32_t)0)

#define SURFACE_TO_NODE(s)              (((s) >> 16) & 0xFF)
#define SURFACE_TO_HOST(s)              (((s) >> 24) & 0xFF)

#define SURFACE_ID_OUR_FLAG             0x40000000
#define SURFACE_ID_MAX                  0x00ffffff


typedef enum constructor_state_e        constructor_state_t;
typedef enum match_e                    match_t;

typedef struct ctrl_surface_s           ctrl_surface_t;
typedef struct ctrl_layer_s             ctrl_layer_t;
typedef struct ctrl_screen_s            ctrl_screen_t;
typedef struct app_surface_s            app_surface_t;
typedef struct application_s            application_t;
typedef struct constructor_s            constructor_t;
typedef struct layer_iterator_s         layer_iterator_t;

struct mrp_glm_window_manager_s {
    MRP_WAYLAND_WINDOW_MANAGER_COMMON;
    application_t *app;
    mrp_list_hook_t constructors;
    mrp_htbl_t *surfaces;
    mrp_htbl_t *layers;
    mrp_htbl_t *screens;
};

struct ctrl_surface_s {
    mrp_wayland_t *wl;
    struct ivi_controller_surface *ctrl_surface;
    struct wl_surface *wl_surface;
    uint32_t id;
    int32_t pid;
    char *title;
    bool ready;
};

struct ctrl_layer_s {
    mrp_wayland_t *wl;
    struct ivi_controller_layer *ctrl_layer;
    int32_t id;
    char *name;
};

struct ctrl_screen_s {
    mrp_wayland_t *wl;
    struct ivi_controller_screen *ctrl_screen;
    int32_t id;
    uint32_t output_index;
    int32_t width, height;
};

struct app_surface_s {
    MRP_WAYLAND_OBJECT_COMMON;
};

struct application_s {
    MRP_WAYLAND_OBJECT_COMMON;
};

enum constructor_state_e {
    CONSTRUCTOR_FAILED = 0,     /* handle request failed */
    CONSTRUCTOR_INCOMPLETE,     /* pid is set but title is missing */
    CONSTRUCTOR_TITLED,         /* pid and title set */
    CONSTRUCTOR_REQUESTED,      /* native handle was requested */
    CONSTRUCTOR_HANDLED,        /* got native handle */
    CONSTRUCTOR_BOUND,          /* got native handle and ivi surface created */
};

struct constructor_s {
    mrp_list_hook_t link;
    mrp_glm_window_manager_t *wm;
    int32_t pid;
    char *title;
    uint32_t id_surface;
    struct wl_surface *wl_surface;
    constructor_state_t state;
    bool multisurface;
    mrp_timer_t *timer;
};

enum match_e {
    DOES_NOT_MATCH = 0,
    DOES_MATCH = 1
};

struct layer_iterator_s {
    mrp_glm_window_manager_t *wm;
    mrp_wayland_output_t *out;
    ctrl_screen_t *scr;
};



static bool window_manager_constructor(mrp_wayland_t *,mrp_wayland_object_t *);
static void ctrl_screen_callback(void *, struct ivi_controller *, uint32_t,
                                 struct ivi_controller_screen *);
static void ctrl_layer_callback(void *, struct ivi_controller *, uint32_t);
static void ctrl_surface_callback(void *, struct ivi_controller *, uint32_t);
static void ctrl_error_callback(void *, struct ivi_controller *, int32_t,
                                int32_t, int32_t, const char *);
static void ctrl_handle_callback(void *, struct ivi_controller *,
                                 struct wl_surface *);

static ctrl_surface_t *surface_create(mrp_glm_window_manager_t *,
                                      struct ivi_controller *,
                                      constructor_t *);
static void surface_destroy(mrp_glm_window_manager_t *, ctrl_surface_t *);
static ctrl_surface_t *surface_find(mrp_glm_window_manager_t *, uint32_t);

static void surface_visibility_callback(void*, struct ivi_controller_surface*,
                                     int32_t);
static void surface_opacity_callback(void *, struct ivi_controller_surface *,
                                     wl_fixed_t);
static void surface_source_rectangle_callback(void *,
                                           struct ivi_controller_surface *,
                                           int32_t,int32_t,int32_t,int32_t);
static void surface_destination_rectangle_callback(void *,
                                           struct ivi_controller_surface *,
                                           int32_t,int32_t, int32_t,int32_t);
static void surface_configuration_callback(void *,
                                   struct ivi_controller_surface *,
                                   int32_t, int32_t);
static void surface_orientation_callback(void *,
                                 struct ivi_controller_surface *,
                                 int32_t);
static void surface_pixelformat_callback(void *,
                                 struct ivi_controller_surface *,
                                 int32_t);
static void surface_added_to_layer_callback(void *,
                                    struct ivi_controller_surface *,
                                    struct ivi_controller_layer *);
static void surface_stats_callback(void *, struct ivi_controller_surface *,
                                   uint32_t, uint32_t, uint32_t,
                                   uint32_t, const char *);
static void surface_destroyed_callback(void *,struct ivi_controller_surface *);
static void surface_content_callback(void *, struct ivi_controller_surface *,
                                     int32_t);
static void surface_input_focus_callback(void *,
                                 struct ivi_controller_surface *,
                                 int32_t);

static ctrl_layer_t *layer_create(mrp_glm_window_manager_t *,
                                  mrp_wayland_layer_t *, ctrl_screen_t *);
static void layer_destroy(mrp_glm_window_manager_t *, ctrl_layer_t *);
static void layer_visibility_callback(void *, struct ivi_controller_layer *,
                                      int32_t);
static void layer_opacity_callback(void *, struct ivi_controller_layer *,
                                   wl_fixed_t opacity);
static void layer_source_rectangle_callback(void *,
                                            struct ivi_controller_layer *,
                                            int32_t,int32_t, int32_t,int32_t);
static void layer_destination_rectangle_callback(void *,
                                            struct ivi_controller_layer *,
                                            int32_t,int32_t, int32_t,int32_t);
static void layer_configuration_callback(void *, struct ivi_controller_layer *,
                                         int32_t, int32_t);
static void layer_orientation_callback(void *, struct ivi_controller_layer *,
                                       int32_t);
static void layer_added_to_screen_callback(void*, struct ivi_controller_layer*,
                                           struct wl_output *);
static void layer_destroyed_callback(void *, struct ivi_controller_layer *);


static uint32_t surface_id_generate(void);
static bool surface_id_is_ours(uint32_t);
static char *surface_id_print(uint32_t, char *, size_t);

static bool application_manager_constructor(mrp_wayland_t *,
                                            mrp_wayland_object_t *);
static void shell_info_callback(void *, struct ivi_application *,
                                int32_t, const char *);

static constructor_t *constructor_create(mrp_glm_window_manager_t *,
                                         constructor_state_t,
                                         int32_t, const char *);
static void constructor_destroy(constructor_t *);
static void constructor_timeout(mrp_timer_t *, void *);
static constructor_t *constructor_find_first(mrp_glm_window_manager_t *,
                                             match_t,
                                             constructor_state_t);
static constructor_t *constructor_find_bound(mrp_glm_window_manager_t *,
                                             uint32_t);
static void constructor_issue_next_request(mrp_glm_window_manager_t *);
static bool constructor_bind_ivi_surface(mrp_glm_window_manager_t *,
                                         constructor_t *);
static char *constructor_state_str(constructor_state_t);


static ctrl_layer_t *find_layer(mrp_glm_window_manager_t *,
                                mrp_wayland_layer_update_t *);
static void layer_request(mrp_wayland_layer_t *,
                          mrp_wayland_layer_update_t *);


static void window_request(mrp_wayland_window_t *,
                           mrp_wayland_window_update_t *,
                           mrp_wayland_animation_t *, uint32_t);


static void buffer_request(mrp_wayland_window_manager_t *, const char *,
                           uint32_t, uint32_t);

static uint32_t sid_hash(const void *);
static uint32_t lid_hash(const void *);
static uint32_t oid_hash(const void *);
static int id_compare(const void *, const void *);


bool mrp_glm_window_manager_register(mrp_wayland_t *wl)
{
    mrp_wayland_factory_t factory;

    factory.size = sizeof(mrp_glm_window_manager_t);
    factory.interface = &ivi_controller_interface;
    factory.constructor = window_manager_constructor;
    factory.destructor = NULL;
    mrp_wayland_register_interface(wl, &factory);

    factory.size = sizeof(application_t);
    factory.interface = &ivi_application_interface;
    factory.constructor = application_manager_constructor;
    factory.destructor = NULL;
    mrp_wayland_register_interface(wl, &factory);

    return true;
}

static bool window_manager_constructor(mrp_wayland_t *wl,
                                       mrp_wayland_object_t *obj)
{
    static struct ivi_controller_listener listener = {
        .screen        = ctrl_screen_callback,
        .layer         = ctrl_layer_callback,
        .surface       = ctrl_surface_callback,
        .error         = ctrl_error_callback,
        .native_handle = ctrl_handle_callback
    };

    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)obj;
    mrp_wayland_interface_t *wif;
    mrp_htbl_config_t scfg, lcfg, ocfg;
    application_t *app;
    int sts;

    MRP_ASSERT(wm, "invalid argument");

    memset(&scfg, 0, sizeof(scfg));
    scfg.nentry = MRP_WAYLAND_WINDOW_MAX;
    scfg.comp = id_compare;
    scfg.hash = sid_hash;
    scfg.nbucket = MRP_WAYLAND_WINDOW_BUCKETS;

    memset(&lcfg, 0, sizeof(lcfg));
    lcfg.nentry = MRP_WAYLAND_LAYER_MAX;
    lcfg.comp = id_compare;
    lcfg.hash = lid_hash;
    lcfg.nbucket = MRP_WAYLAND_LAYER_BUCKETS;

    memset(&ocfg, 0, sizeof(ocfg));
    ocfg.nentry = MRP_WAYLAND_OUTPUT_MAX;
    ocfg.comp = id_compare;
    ocfg.hash = oid_hash;
    ocfg.nbucket = MRP_WAYLAND_OUTPUT_BUCKETS;

    wm->layer_request = layer_request;
    wm->window_request = window_request;
    wm->buffer_request = buffer_request;

    mrp_list_init(&wm->constructors);

    wm->surfaces = mrp_htbl_create(&scfg);
    wm->layers = mrp_htbl_create(&lcfg);
    wm->screens = mrp_htbl_create(&ocfg);

    MRP_ASSERT(wm->surfaces && wm->layers, "failed to create hash table");

    sts = ivi_controller_add_listener((struct ivi_controller *)wm->proxy,
                                      &listener, wm);

    if (sts < 0)
        return false;

    mrp_wayland_register_window_manager(wl, (mrp_wayland_window_manager_t*)wm);

    wif = mrp_htbl_lookup(wl->interfaces,
                          (void *)ivi_application_interface.name);

    if (wif && !mrp_list_empty(&wif->object_list)) {
        app = mrp_list_entry(wif->object_list.next,
                             application_t, interface_link);

        mrp_debug("registering application interface to window manager");

        wm->app = app;
    }

    return true;
}

static int layer_iterator_cb(void *key, void *object, void *user_data)
{
    mrp_wayland_layer_t *layer = (mrp_wayland_layer_t *)object;
    layer_iterator_t *it = (layer_iterator_t *)user_data;

    MRP_UNUSED(key);

    if (!strcmp(layer->outputname, it->out->outputname))
        layer_create(it->wm, layer, it->scr);

    return MRP_HTBL_ITER_MORE;
}

static void ctrl_screen_callback(void *data,
                                 struct ivi_controller *ivi_controller,
                                 uint32_t id_screen,
                                 struct ivi_controller_screen *screen)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)data;
    mrp_wayland_t *wl;
    mrp_wayland_output_t *output;
    ctrl_screen_t *s;
    layer_iterator_t it;

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ivi_controller == (struct ivi_controller *)wm->proxy,
               "confused with data structures");
    
    wl = wm->interface->wl;

    mrp_debug("id_screen=%u screen=%p", id_screen, screen);

    if (!(output = mrp_wayland_output_find_by_id(wl, id_screen))) {
        mrp_log_warning("system-controller: can't find output for screen %u",
                        id_screen);
        return;
    }

    if (!output->name || output->width <= 0 || output->height <= 0) {
        mrp_log_warning("system-controller: incomplete output (id %u)",
                        id_screen);
        return;
    }

    if (!(s = mrp_allocz(sizeof(ctrl_screen_t)))) {
        mrp_log_error("system-controller: can't allocate memory for screen");
        return;
    }

    s->wl = wl;
    s->ctrl_screen = screen;
    s->id = id_screen;
    s->output_index = output->index;
    s->width = output->width;
    s->height = output->height;
    
    it.wm = wm;
    it.out = output;
    it.scr = s;
    mrp_htbl_foreach(wl->layers.by_type, layer_iterator_cb, &it);

    mrp_wayland_flush(wl);
}

static void ctrl_layer_callback(void *data,
                                struct ivi_controller *ivi_controller,
                                uint32_t id_layer)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)data;
    mrp_wayland_t *wl;

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ivi_controller == (struct ivi_controller *)wm->proxy,
               "confused with data structures");
    
    wl = wm->interface->wl;

    mrp_debug("id_layer=%u", id_layer);
}

static void ctrl_surface_callback(void *data,
                                  struct ivi_controller *ivi_controller,
                                  uint32_t id_surface)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)data;
    mrp_wayland_t *wl;
    ctrl_surface_t *sf;
    mrp_list_hook_t *c, *n;
    constructor_t *entry;
    struct ivi_surface *ivi_surface;
    char buf[256];

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ivi_controller == (struct ivi_controller *)wm->proxy,
               "confused with data structures");
    
    wl = wm->interface->wl;

    mrp_debug("id_surface=%s", surface_id_print(id_surface, buf,sizeof(buf)));

    mrp_list_foreach(&wm->constructors, c, n) {
        entry = mrp_list_entry(c, constructor_t, link);
        
        if (entry->state == CONSTRUCTOR_FAILED)
            continue;
        
        if (entry->state == CONSTRUCTOR_HANDLED) {
            mrp_debug("found handled constructor entry");

            entry->id_surface = id_surface;

            if (constructor_bind_ivi_surface(wm, entry)) {
                entry->state = CONSTRUCTOR_BOUND;
                surface_create(wm, ivi_controller, entry);
            }

            constructor_destroy(entry);
            constructor_issue_next_request(wm);
            break;
        }

        if  (entry->state == CONSTRUCTOR_REQUESTED) {
            mrp_debug("found requested constructor");

            entry->id_surface = id_surface;

            break;
        }

        mrp_debug("ignoring surface handle");

        break;
    }
}


static void ctrl_error_callback(void *data,
                                struct ivi_controller *ivi_controller,
                                int32_t object_id,
                                int32_t object_type,
                                int32_t error_code,
                                const char *error_text)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)data;
    mrp_wayland_t *wl;
    const char *type_str;
    mrp_list_hook_t *c, *n;
    constructor_t *entry;
    bool request_already_removed;

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ivi_controller == (struct ivi_controller *)wm->proxy,
               "confused with data structures");
    
    wl = wm->interface->wl;

    switch (object_type) {
    case IVI_CONTROLLER_OBJECT_TYPE_SURFACE:   type_str = "surface";     break;
    case IVI_CONTROLLER_OBJECT_TYPE_LAYER:     type_str = "layer";       break;
    case IVI_CONTROLLER_OBJECT_TYPE_SCREEN:    type_str = "screen";      break;
    default:                                   type_str = "<unknown>";   break;
    }

    mrp_debug("%s %d error %d: %s", type_str, object_id,
              error_code, error_text ? error_text:"???");

    switch (object_type) {

    case IVI_CONTROLLER_OBJECT_TYPE_SURFACE:
        if (error_code  == IVI_CONTROLLER_ERROR_CODE_NATIVE_HANDLE_END) {
            mrp_debug("ignoring native_handle_list end marker");
            return;
        }

        request_already_removed = false;

        mrp_list_foreach(&wm->constructors, c, n) {
            entry = mrp_list_entry(c, constructor_t, link);

            switch (entry->state) {

            case CONSTRUCTOR_REQUESTED:
                if (request_already_removed) {
                    mrp_log_error("system-controller: multiple requests "
                                  "in surface queue");
                }
                else {
                    constructor_destroy(entry);
                    request_already_removed = true;
                }
                break;

            case CONSTRUCTOR_TITLED:
            case CONSTRUCTOR_FAILED:
                /* nothing to do with these */
                break;

            default:
                entry->state = CONSTRUCTOR_FAILED;
                break;
            }
        }

        constructor_issue_next_request(wm);
        
        break;

    default:
        break;
    }
}

static void ctrl_handle_callback(void *data,
                                 struct ivi_controller *ivi_controller,
                                 struct wl_surface *wl_surface)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)data;
    mrp_wayland_t *wl;
    application_t *app;
    const char *type_str;
    constructor_t *reqsurf, *appsurf;
    int nappsurf;
    mrp_list_hook_t *s, *n;
    constructor_t *entry;
    char buf[256];

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ivi_controller == (struct ivi_controller *)wm->proxy,
               "confused with data structures");
    
    wl = wm->interface->wl;

    mrp_debug("wl_surface=%p", wl_surface);

    if (!(app = wm->app)) {
        mrp_log_error("system-controller: ivi application interface is down "
                      "when %s() is called", __FUNCTION__);
        return;
    }

    reqsurf = constructor_find_first(wm, DOES_MATCH, CONSTRUCTOR_REQUESTED);

    if (!reqsurf) {
        mrp_debug("confused: no pendig request");
        return;
    }

    nappsurf = 0;

    mrp_list_foreach(&wm->constructors, s, n) {
        entry = mrp_list_entry(s, constructor_t, link);

        if (entry != reqsurf && entry->pid == reqsurf->pid) {
            nappsurf++;
            appsurf = entry;
        }
    }

    if (nappsurf == 1 && !reqsurf->multisurface && appsurf->id_surface != 0) {
        /* title change */

        // update name of appsurf

        constructor_destroy(reqsurf);
        constructor_issue_next_request(wm);
    }
    else {
        /* create an ivi-surface and bind it to wl_surface */
        reqsurf->wl_surface = wl_surface;

        if (!reqsurf->id_surface) {
            mrp_debug("id is unknown => set state handled "
                      "(postpone ivi-surface creation)");

            reqsurf->state = CONSTRUCTOR_HANDLED;
        }
        else {
            if (constructor_bind_ivi_surface(wm, reqsurf)) {
                reqsurf->state = CONSTRUCTOR_BOUND;
                surface_create(wm, ivi_controller, reqsurf);
            }

            constructor_destroy(reqsurf);
            constructor_issue_next_request(wm);
        }
    }
}


static ctrl_surface_t *surface_create(mrp_glm_window_manager_t *wm,
                                      struct ivi_controller *ctrl,
                                      constructor_t *c)
{
    static struct ivi_controller_surface_listener listener =  {
        .visibility            =  surface_visibility_callback,
        .opacity               =  surface_opacity_callback,
        .source_rectangle      =  surface_source_rectangle_callback,
        .destination_rectangle =  surface_destination_rectangle_callback,
        .configuration         =  surface_configuration_callback,
        .orientation           =  surface_orientation_callback,
        .pixelformat           =  surface_pixelformat_callback,
        .layer                 =  surface_added_to_layer_callback,
        .stats                 =  surface_stats_callback,
        .destroyed             =  surface_destroyed_callback,
        .content               =  surface_content_callback,
        .input_focus           =  surface_input_focus_callback
    };

    mrp_wayland_t *wl;
    struct ivi_controller_surface *ctrl_surface;
    ctrl_surface_t *sf;
    char id_str[256];
    int sts;

    MRP_ASSERT(wm && wm->interface && wm->interface->wl && ctrl && c,
               "invalid argument");

    wl = wm->interface->wl;

    surface_id_print(c->id_surface, id_str, sizeof(id_str));

    mrp_debug("create surface %s ...", id_str);

    if (surface_find(wm, c->id_surface)) {
        mrp_log_error("system-controller: attempt to create multiple times "
                      "surface %s", id_str);
        return NULL;
    }

    if (!(ctrl_surface = ivi_controller_surface_create(ctrl, c->id_surface))) {
        mrp_log_error("system-controller: failed to create controller "
                      "surface for surface %s", id_str);
        return NULL;
    }

    mrp_debug("ctrl_surface %p was created for surface %s",
              ctrl_surface, id_str);

    if (!(sf = mrp_allocz(sizeof(ctrl_surface_t)))) {
        mrp_log_error("system-controller: failed to allocate memory "
                      "for surface %s", id_str);
        return NULL;
    }

    sf->wl = wl;
    sf->ctrl_surface = ctrl_surface;
    sf->wl_surface  = c->wl_surface;
    sf->id = c->id_surface;
    sf->pid = c->pid;
    sf->title = mrp_strdup(c->title ? c->title : "");

    if (!mrp_htbl_insert(wm->surfaces, &sf->id, sf)) {
        mrp_log_error("system-controller: hashmap insertion error when "
                      "trying to create surface %s ", id_str);
        mrp_free(sf);
        return NULL;
    }

    if (ivi_controller_surface_add_listener(ctrl_surface, &listener, sf) < 0) {
        mrp_log_error("system-controller: failed to create surface %s "
                      "(can't listen to surface)", id_str);

        mrp_htbl_remove(wm->surfaces, &sf->id, false);

        mrp_free(sf);

        return NULL;
    }

    mrp_wayland_flush(wl);

    return sf;
}


static void surface_destroy(mrp_glm_window_manager_t *wm, ctrl_surface_t *sf)
{
    ctrl_surface_t *d;
    char buf[256];

    if (wm && wm->surfaces && sf) {
        mrp_debug("surface %s going to be destroyed",
                  surface_id_print(sf->id, buf, sizeof(buf)));

        if (!(d = mrp_htbl_remove(wm->surfaces, &sf->id, false)) || sf != d) {
            mrp_log_error("system-controller: confused with data structures "
                          "(surface hashtable entry mismatch)");
        }
        else {
            mrp_free(sf->title);
            mrp_free(sf);
        }
    }
}

static ctrl_surface_t *surface_find(mrp_glm_window_manager_t *wm,
                                    uint32_t id_surface)
{
    ctrl_surface_t *sf;

    if (!wm || !wm->surfaces)
        sf = NULL;
    else
        sf = mrp_htbl_lookup(wm->surfaces, &id_surface);

    return sf;
}


static void surface_visibility_callback(void *data,
                                 struct ivi_controller_surface *ctrl_surface,
                                 int32_t visibility)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) visibility=%d", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), visibility);
}

static void surface_opacity_callback(void *data,
                                 struct ivi_controller_surface *ctrl_surface,
                                 wl_fixed_t opacity)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");
    mrp_debug("ctrl_surface=%p (id=%s) opacity=%d", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), opacity);
}

static void surface_source_rectangle_callback(void *data,
                                   struct ivi_controller_surface *ctrl_surface,
				   int32_t x,
				   int32_t y,
				   int32_t width,
				   int32_t height)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) x=%d y=%d width=%d height=%d",
              ctrl_surface, surface_id_print(sf->id, buf, sizeof(buf)),
              x, y, width, height);
}

static void surface_destination_rectangle_callback(void *data,
                                   struct ivi_controller_surface *ctrl_surface,
                                   int32_t x,
				   int32_t y,
				   int32_t width,
				   int32_t height)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) x=%d y=%d width=%d height=%d",
              ctrl_surface, surface_id_print(sf->id, buf, sizeof(buf)),
              x, y, width, height);
}

static void surface_configuration_callback(void *data,
                                   struct ivi_controller_surface *ctrl_surface,
                                   int32_t width,
                                   int32_t height)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) width=%d height=%d", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), width, height);
}

static void surface_orientation_callback(void *data,
                                 struct ivi_controller_surface *ctrl_surface,
                                 int32_t orientation)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) orientation=%d", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), orientation);
}

static void surface_pixelformat_callback(void *data,
                                 struct ivi_controller_surface *ctrl_surface,
                                 int32_t pixelformat)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) pixelformat=%d", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), pixelformat);
}

static void surface_added_to_layer_callback(void *data,
                                   struct ivi_controller_surface *ctrl_surface,
                                   struct ivi_controller_layer *layer)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) layer=%p", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), layer);
}

static void surface_stats_callback(void *data,
                                   struct ivi_controller_surface *ctrl_surface,
                                   uint32_t redraw_count,
                                   uint32_t frame_count,
                                   uint32_t update_count,
                                   uint32_t pid,
                                   const char *process_name)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s)"
              "redraw_count=%u frame_count=%u update_count=%u "
              "pid=%u process_name='%s'",
              ctrl_surface, surface_id_print(sf->id, buf, sizeof(buf)),
              redraw_count, frame_count, update_count, pid,
              process_name ? process_name : "<null>");

    if (sf->pid != pid) {
        mrp_debug("confused with data structures (mismatching pids)");
        return;
    }
}


static void surface_destroyed_callback(void *data,
                                   struct ivi_controller_surface *ctrl_surface)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s)", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)));

    surface_destroy(wm, sf);
}

static void surface_content_callback(void *data,
                             struct ivi_controller_surface *ctrl_surface,
                             int32_t content_state)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) content_state=%d", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), content_state);
}

static void surface_input_focus_callback(void *data,
                                 struct ivi_controller_surface *ctrl_surface,
                                 int32_t enabled)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) enabled=%d", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), enabled);
}


static ctrl_layer_t *layer_create(mrp_glm_window_manager_t *wm,
                                  mrp_wayland_layer_t *layer,
                                  ctrl_screen_t *s)
{
    static struct ivi_controller_layer_listener listener = {
        .visibility            =  layer_visibility_callback,
        .opacity               =  layer_opacity_callback,
        .source_rectangle      =  layer_source_rectangle_callback,
        .destination_rectangle =  layer_destination_rectangle_callback,
        .configuration         =  layer_configuration_callback,
        .orientation           =  layer_orientation_callback,
        .screen                =  layer_added_to_screen_callback,
        .destroyed             =  layer_destroyed_callback
    };

    struct ivi_controller_layer *ctrl_layer;
    ctrl_layer_t *ly;

    mrp_debug("create and link layer %s (id=0x%08x size=%dx%d) to screen %d",
              layer->name, layer->layerid, s->width, s->height, s->id);

    ctrl_layer = ivi_controller_layer_create((struct ivi_controller*)wm->proxy,
                                             layer->layerid,
                                             s->width, s->height);
    if (!ctrl_layer) {
        mrp_log_error("system-controller: can't create controller layer for "
                      "%s layer (0x%08x)", layer->name, layer->layerid);
        return NULL;
    }

    if (!(ly = mrp_allocz(sizeof(ctrl_layer_t)))) {
        mrp_log_error("system-controller: can't allocate memory for layer "
                      "%s (0x%08x)", layer->name, layer->layerid);
        return NULL;
    }

    ly->wl = layer->wl;
    ly->ctrl_layer = ctrl_layer;
    ly->id = layer->layerid;
    ly->name = mrp_strdup(layer->name);

    if (!mrp_htbl_insert(wm->layers, &ly->id, ly)) {
        mrp_log_error("system-controller: failed to instantiate layer %s "
                      "(0x%08x): already exists", layer->name, layer->layerid);
        mrp_free(ly);
        return NULL;
    }

    if (ivi_controller_layer_add_listener(ctrl_layer, &listener, ly) < 0) {
        mrp_debug("can't listen to layer %s (0x%08x)",
                  layer->name, layer->layerid);
        mrp_htbl_remove(wm->layers, &ly->id, false);
        mrp_free(ly);
        return NULL;
    }

    ivi_controller_screen_add_layer(s->ctrl_screen, ctrl_layer);

    return ly;
}

static void layer_destroy(mrp_glm_window_manager_t *wm, ctrl_layer_t *ly)
{
    ctrl_layer_t *d;

    if (wm && wm->layers && ly) {
        mrp_debug("layer %s 0x%08x going to be destroyed", ly->name, ly->id);

        if (!(d = mrp_htbl_remove(wm->layers, &ly->id, false)) || ly != d) {
            mrp_log_error("system-controller: confused with data structures "
                          "(layer hashtable mismatch)");
        }
        else {
            mrp_free(ly->name);
            mrp_free(ly);
        }
    }
}

static void layer_visibility_callback(void *data,
                                      struct ivi_controller_layer *ctrl_layer,
                                      int32_t visibility)
{
    ctrl_layer_t *ly = (ctrl_layer_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(ly && ly->wl, "invalid argument");
    MRP_ASSERT(ctrl_layer == ly->ctrl_layer,
               "confused with data structures");

    wl = ly->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_layer=%p (%s %d) visibility=%d", ctrl_layer,
              ly->name, ly->id, visibility);
}

static void layer_opacity_callback(void *data,
                                   struct ivi_controller_layer *ctrl_layer,
                                   wl_fixed_t opacity)
{
    ctrl_layer_t *ly = (ctrl_layer_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(ly && ly->wl, "invalid argument");
    MRP_ASSERT(ctrl_layer == ly->ctrl_layer,
               "confused with data structures");

    wl = ly->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_layer=%p (%s %d) opacity=%d", ctrl_layer,
              ly->name, ly->id, opacity);
}

static void layer_source_rectangle_callback(void *data,
                                       struct ivi_controller_layer *ctrl_layer,
			               int32_t x,
			               int32_t y,
			               int32_t width,
			               int32_t height)
{
    ctrl_layer_t *ly = (ctrl_layer_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(ly && ly->wl, "invalid argument");
    MRP_ASSERT(ctrl_layer == ly->ctrl_layer,
               "confused with data structures");

    wl = ly->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_layer=%p (%s %d) x=%d y=%d width=%d height=%d", ctrl_layer,
              ly->name, ly->id, x,y, width,height);
}

static void layer_destination_rectangle_callback(void *data,
                                       struct ivi_controller_layer *ctrl_layer,
			               int32_t x,
			               int32_t y,
			               int32_t width,
			               int32_t height)
{
    ctrl_layer_t *ly = (ctrl_layer_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(ly && ly->wl, "invalid argument");
    MRP_ASSERT(ctrl_layer == ly->ctrl_layer,
               "confused with data structures");

    wl = ly->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_layer=%p (%s %d) x=%d y=%d width=%d height=%d", ctrl_layer,
              ly->name, ly->id, x,y, width,height);
}

static void layer_configuration_callback(void *data,
                                      struct ivi_controller_layer *ctrl_layer,
			              int32_t width,
			              int32_t height)
{
    ctrl_layer_t *ly = (ctrl_layer_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(ly && ly->wl, "invalid argument");
    MRP_ASSERT(ctrl_layer == ly->ctrl_layer,
               "confused with data structures");

    wl = ly->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_layer=%p (%s %d) width=%d height=%d", ctrl_layer,
              ly->name, ly->id, width,height);
}

static void layer_orientation_callback(void *data,
                                       struct ivi_controller_layer *ctrl_layer,
                                       int32_t orientation)
{
    ctrl_layer_t *ly = (ctrl_layer_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(ly && ly->wl, "invalid argument");
    MRP_ASSERT(ctrl_layer == ly->ctrl_layer,
               "confused with data structures");

    wl = ly->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_layer=%p (%s %d) orientation=%d", ctrl_layer,
              ly->name, ly->id, orientation);
}

static void layer_added_to_screen_callback(void *data,
                                      struct ivi_controller_layer *ctrl_layer,
		                      struct wl_output *screen)
{
    ctrl_layer_t *ly = (ctrl_layer_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(ly && ly->wl, "invalid argument");
    MRP_ASSERT(ctrl_layer == ly->ctrl_layer,
               "confused with data structures");

    wl = ly->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_layer=%p (%s %d) screen=%p", ctrl_layer,
              ly->name, ly->id, screen);
}

static void layer_destroyed_callback(void *data,
                                     struct ivi_controller_layer *ctrl_layer)
{
    ctrl_layer_t *ly = (ctrl_layer_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(ly && ly->wl, "invalid argument");
    MRP_ASSERT(ctrl_layer == ly->ctrl_layer,
               "confused with data structures");

    wl = ly->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_layer=%p (%s %d)", ctrl_layer, ly->name, ly->id);
}


static bool application_manager_constructor(mrp_wayland_t *wl,
                                            mrp_wayland_object_t *obj)
{
    static struct ivi_application_listener listener =  {
        .wl_shell_info = shell_info_callback
    };

    application_t *app = (application_t *)obj;
    mrp_glm_window_manager_t *wm;
    int sts;

    MRP_ASSERT(wl && app, "invalid argument");

    sts = ivi_application_add_listener((struct ivi_application *)app->proxy,
                                       &listener, app);
    if (sts < 0)
        return false;

    if ((wm = (mrp_glm_window_manager_t *)wl->wm)) {
        mrp_debug("registering application interface to window manager");
        wm->app = app;
    }

    return true;
}

static void shell_info_callback(void *data,
                                struct ivi_application *ivi_application,
                                int32_t pid,
                                const char *title)
{
    application_t *app = (application_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    mrp_list_hook_t *c, *n;
    constructor_t *entry;
    constructor_state_t state;

    MRP_ASSERT(pid > 0 && title, "invalid argument");
    MRP_ASSERT(app && app->interface && app->interface->wl,"invalid argument");
    MRP_ASSERT(ivi_application == (struct ivi_application *)app->proxy,
               "confused with data structures");

    wl = app->interface->wl;

    mrp_debug("pid=%d title='%s'", pid, title ? title : "<null>");

    if (!(wm = (mrp_glm_window_manager_t *)wl->wm)) {
        mrp_debug("controller interface is not ready");
        return;
    }

    mrp_list_foreach(&wm->constructors, c, n) {
        entry = mrp_list_entry(c, constructor_t, link);

        if (pid == entry->pid) {
            if (entry->state == CONSTRUCTOR_INCOMPLETE) {
                /* title has not been set for this entry */
                if (!title || *title == '\0') {
                    mrp_debug("nothing to do "
                              "(already have an incomplete entry)");
                    return;
                }

                mrp_debug("setting title for incomplete constructor");
                entry->state = CONSTRUCTOR_TITLED;
                entry->title = mrp_strdup(title);

                constructor_issue_next_request(wm);

                return;
            }
            else {
                /* the title is already set for this entry */
                if (title && !strcmp(entry->title, title)) {
                    mrp_debug("nothing to do (same title)");
                    return;
                }
            }
        }
    }

    state = (title && *title) ? CONSTRUCTOR_TITLED : CONSTRUCTOR_INCOMPLETE;

    constructor_create(wm, state, pid, title);
    constructor_issue_next_request(wm);
}


static uint32_t surface_id_generate(void)
{
#define FIRST_ID   (SURFACE_ID_OUR_FLAG | 1)
#define MAX_ID     (SURFACE_ID_OUR_FLAG | SURFACE_ID_MAX)

    static uint32_t id = FIRST_ID;

    if (id >= MAX_ID)
        id = FIRST_ID;

    return id++;

#undef MAX_ID
#undef FIRST_ID
}

static bool surface_id_is_ours(uint32_t id)
{
    return (id & SURFACE_ID_OUR_FLAG) ? true : false;
}

static char *surface_id_print(uint32_t id, char *buf, size_t len)
{
    snprintf(buf, len, "%u(%s-id-%u)", id,
             surface_id_is_ours(id) ? "our":"native",
             (id & SURFACE_ID_MAX));
    return buf;
}

static constructor_t *constructor_create(mrp_glm_window_manager_t *wm,
                                         constructor_state_t state,
                                         int32_t pid,
                                         const char *title)
{
    mrp_wayland_t *wl;
    constructor_t *c;

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");

    wl = wm->interface->wl;

    if (!(c = mrp_allocz(sizeof(constructor_t)))) {
        mrp_log_error("system-controller: can't allocate memory for surface"
                      "(pid=%d title='%s')", pid, title ? title : "<null>");
        return NULL;
    }

    mrp_list_init(&c->link);
    c->wm = wm;
    c->state = state;
    c->pid = pid;
    c->title = title ? mrp_strdup(title) : NULL;
    c->timer = mrp_add_timer(wl->ml, CONSTRUCTOR_TIMEOUT,
                             constructor_timeout, c);

    mrp_list_append(&wm->constructors, &c->link);

    mrp_debug("constructor created (state=%s, pid=%d, title='%s')",
              constructor_state_str(state), pid, title?title:"<null>");

    return c;
}

static void constructor_destroy(constructor_t *c)
{
    char buf[256];

    if (c) {
        mrp_debug("surface %s of application (pid=%d) destroyed",
                  surface_id_print(c->id_surface, buf, sizeof(buf)), c->pid);

        mrp_del_timer(c->timer);

        mrp_list_delete(&c->link);
        mrp_free(c->title);
        
        mrp_free(c);
    }
}

static void constructor_timeout(mrp_timer_t *timer, void *user_data)
{
    constructor_t *c = (constructor_t *)user_data;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(timer && c && c->wm, "invalid argument");
    MRP_ASSERT(timer == c->timer, "confused with data structures");

    wm = c->wm;

    constructor_destroy(c);
    constructor_issue_next_request(wm);
}

static constructor_t *constructor_find_first(mrp_glm_window_manager_t *wm,
                                             match_t match,
                                             constructor_state_t state)
{
    mrp_list_hook_t *c, *n;
    constructor_t *entry;

    mrp_list_foreach(&wm->constructors, c, n) {
        entry = mrp_list_entry(c, constructor_t, link);

        if (( match && state == entry->state) ||
            (!match && state != entry->state))
            return entry;
    }

    return NULL;
}

static constructor_t *constructor_find_bound(mrp_glm_window_manager_t *wm,
                                             uint32_t id_surface)
{
    mrp_list_hook_t *c, *n;
    constructor_t *entry;

    if (id_surface > 0) {
        mrp_list_foreach(&wm->constructors, c, n) {
            entry = mrp_list_entry(c, constructor_t, link);
            
            if (id_surface == entry->id_surface &&
                entry->state == CONSTRUCTOR_BOUND)
                return entry;
        }
    }

    return NULL;
}


static void constructor_issue_next_request(mrp_glm_window_manager_t *wm)
{
    mrp_list_hook_t *c, *n;
    constructor_t *entry;

    mrp_debug("loop through constructors ...");

    mrp_list_foreach(&wm->constructors, c, n) {
        entry = mrp_list_entry(c, constructor_t, link);

        mrp_debug("   state %s", constructor_state_str(entry->state));
        
        if (entry->state == CONSTRUCTOR_TITLED) {
            mrp_debug("      call ivi_controller_get_native_handle"
                      "(pid=%d title='%s')", entry->pid, entry->title);
            
            ivi_controller_get_native_handle((struct ivi_controller*)wm->proxy,
                                             entry->pid, entry->title);
            
            entry->state = CONSTRUCTOR_REQUESTED;

            return;
        }
        
        if (entry->state != CONSTRUCTOR_FAILED)
            break;
    }

    mrp_debug("   do not issue native handle request");
}

static bool constructor_bind_ivi_surface(mrp_glm_window_manager_t *wm,
                                         constructor_t *c)
{
    struct ivi_surface *ivi_surface;
    application_t *app;
    char buf[256];

    if (!wm || !(app = wm->app) || !c)
        return false;

    if (!c->id_surface) {
        mrp_log_error("system-controller: failed to create ivi-surface "
                      "(id_surface not set)");
        return false;
    }

    if (!c->wl_surface) {
        mrp_log_error("system-controller: failed to create ivi-surface "
                      "(wl_surface not set)");
        return false;
    }

    mrp_debug("call ivi_application_surface_create(id_surface=%s surface=%p)",
              surface_id_print(c->id_surface, buf,sizeof(buf)), c->wl_surface);

    ivi_surface = ivi_application_surface_create(
                                      (struct ivi_application *)app->proxy,
                                      c->id_surface, c->wl_surface);
    if (!ivi_surface) {
        mrp_log_error("system-controller: failed to create "
                      "ivi-application-surface (id=%s)",
                      surface_id_print(c->id_surface, buf, sizeof(buf)));
        return false;
    }

    return true;
}

static char *constructor_state_str(constructor_state_t state)
{
    switch (state) {
    case CONSTRUCTOR_FAILED:      return "failed";
    case CONSTRUCTOR_INCOMPLETE:  return "incomplete";
    case CONSTRUCTOR_TITLED:      return "titled";
    case CONSTRUCTOR_REQUESTED:   return "requested";
    case CONSTRUCTOR_HANDLED:     return "handled";
    case CONSTRUCTOR_BOUND:       return "bound";
    default:                      return "<unknown>";
    }
}

static ctrl_layer_t *find_layer(mrp_glm_window_manager_t *wm,
                                mrp_wayland_layer_update_t *u)
{
    if (!(u->mask & MRP_WAYLAND_LAYER_LAYERID_MASK)) {
        mrp_debug("can't find layer (layerid is not set in request)");
        return NULL;
    }

 
    return mrp_htbl_lookup(wm->layers, &u->layerid);
}

static void layer_request(mrp_wayland_layer_t *layer,
                          mrp_wayland_layer_update_t *u)
{
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    mrp_wayland_layer_update_mask_t mask;
    //mrp_wayland_layer_update_mask_t passthrough;
    ctrl_layer_t *l;
    char buf[2048];

    MRP_ASSERT(layer && layer->wm && layer->wm->proxy &&
               layer->wm->interface && layer->wm->interface->wl,
               "invalid argument");

    wm = (mrp_glm_window_manager_t *)layer->wm;
    wl = wm->interface->wl;
    //passthrough = wm->passthrough.layer_request;
    mask = u->mask;

    mrp_wayland_layer_request_print(u, buf, sizeof(buf));
    mrp_debug("request for layer %d update:%s", layer->layerid, buf);

    if (!(l = find_layer(wm, u)))
        mrp_debug("layer request ignored (can't find layer)");
    else {
#if 0
        while (mask) {
            if ((mask & MRP_WAYLAND_LAYER_VISIBLE_MASK)) {
                set_layer_visible(layer, passthrough, u);
                mask &= ~MRP_WAYLAND_LAYER_VISIBLE_MASK;
            }
            else {
                mask = 0;
            }
        }
#endif

        mrp_wayland_flush(wl);
    }
}



static void window_request(mrp_wayland_window_t *win,
                           mrp_wayland_window_update_t *u,
                           mrp_wayland_animation_t *anims,
                           uint32_t framerate)
{
#if 0
    static mrp_wayland_window_update_mask_t visible_mask =
        MRP_WAYLAND_WINDOW_VISIBLE_MASK |
        MRP_WAYLAND_WINDOW_RAISE_MASK   ;
    static mrp_wayland_window_update_mask_t active_mask =
        MRP_WAYLAND_WINDOW_ACTIVE_MASK;
    static mrp_wayland_window_update_mask_t mapped_mask =
        MRP_WAYLAND_WINDOW_MAPPED_MASK;
    static mrp_wayland_window_update_mask_t area_mask =
        MRP_WAYLAND_WINDOW_AREA_MASK;
    static mrp_wayland_window_update_mask_t geometry_mask =
        MRP_WAYLAND_WINDOW_NODEID_MASK   |
        MRP_WAYLAND_WINDOW_POSITION_MASK |
        MRP_WAYLAND_WINDOW_SIZE_MASK     ;
    static mrp_wayland_window_update_mask_t layer_mask =
        MRP_WAYLAND_WINDOW_LAYER_MASK;

    mrp_wayland_t *wl;
    mrp_wayland_window_manager_t *wm;
    mrp_wayland_layer_update_mask_t passthrough;
    mrp_wayland_window_update_mask_t mask;
    char wbuf[2048];
    char abuf[1024];

    MRP_ASSERT(win && win->wm && win->wm->proxy && win->wm->interface &&
               win->wm->interface->wl && u, "invalid argument");

    wm = win->wm;
    wl = wm->interface->wl;
    passthrough = wm->passthrough.window_request;
    mask = u->mask;

    mrp_wayland_window_request_print(u, wbuf, sizeof(wbuf));
    mrp_wayland_animation_print(anims, abuf, sizeof(abuf));
    mrp_debug("request for window %d update:%s\n   animations:%s",
              win->surfaceid, wbuf, abuf);

    while (mask) {
        if ((mask & layer_mask)) {
            set_window_layer(win, passthrough, u);
            mask &= ~layer_mask;
        }
        else if ((mask & mapped_mask)) {
            set_window_mapped(win, passthrough, u, framerate);
            mask &= ~mapped_mask;
        }
        else if ((mask & area_mask))  {
            set_window_area(win, passthrough, u, anims);
            mask &= ~(area_mask | geometry_mask);
        }
        else if ((mask & geometry_mask)) {
            set_window_geometry(win, passthrough, u, anims);
            mask &= ~geometry_mask;
        }
        else if ((mask & visible_mask)) {
            set_window_visible(win, passthrough, u, anims);
            mask &= ~visible_mask;
        }
        else if ((mask & active_mask)) {
            set_window_active(win, passthrough, u);
            mask &= ~active_mask;
        }
        else {
            mask = 0;
        }
    }

    mrp_wayland_flush(wl);
#endif
}


static void buffer_request(mrp_wayland_window_manager_t *wm,
                           const char *shmname,
                           uint32_t bufsize,
                           uint32_t bufnum)
{
    MRP_ASSERT(wm && wm->proxy && shmname, "invalid argument");

    mrp_warning("system-controller: buffer_request is not supported in "
                "Genivi Layer Management");
}


static uint32_t sid_hash(const void *pkey)
{
    uint32_t key = *(uint32_t *)pkey;

    return key % MRP_WAYLAND_WINDOW_BUCKETS;
}

static uint32_t lid_hash(const void *pkey)
{
    uint32_t key = *(uint32_t *)pkey;

    return key % MRP_WAYLAND_LAYER_BUCKETS;
}


static uint32_t oid_hash(const void *pkey)
{
    uint32_t key = *(uint32_t *)pkey;

    return key % MRP_WAYLAND_OUTPUT_BUCKETS;
}


static int id_compare(const void *pkey1, const void *pkey2)
{
    int32_t key1 = *(int32_t *)pkey1;
    int32_t key2 = *(int32_t *)pkey2;

    return (key1 == key2) ? 0 : ((key1 < key2) ? -1 : 1);
}
