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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>

#include <murphy/common.h>

#include <genivi-shell/ivi-controller-client-protocol.h>
#include <genivi-shell/ivi-application-client-protocol.h>
#include <ico-uxf-weston-plugin/ico_window_mgr-client-protocol.h>

#include <aul/aul.h>

#include "glm-window-manager.h"
#include "layer.h"
#include "output.h"
#include "animation.h"
#include "window.h"
#include "window-manager.h"
#include "area.h"

#define MAX_COORDINATE                  16383

#define SURFACELESS_TIMEOUT             1000  /* ms */
#define CONSTRUCTOR_TIMEOUT             10000 /* ms */
#define TITLE_TIMEOUT                   1500  /* ms */

#define INVALID_INDEX                   (~(uint32_t)0)

#define SURFACE_TO_NODE(s)              (((s) >> 16) & 0xFF)
#define SURFACE_TO_HOST(s)              (((s) >> 24) & 0xFF)

#define SURFACE_ID_OUR_FLAG             0x70000000
#define SURFACE_ID_MAX                  0x00ffffff

#define VISIBLE                         true
#define HIDDEN                          false

typedef enum constructor_state_e        constructor_state_t;
typedef enum match_e                    match_t;

typedef struct ctrl_surface_s           ctrl_surface_t;
typedef struct ctrl_layer_s             ctrl_layer_t;
typedef struct ctrl_screen_s            ctrl_screen_t;
typedef struct app_surface_s            app_surface_t;
typedef struct application_s            application_t;
typedef struct ico_extension_s          ico_extension_t;
typedef struct constructor_s            constructor_t;
typedef struct screen_layer_iterator_s  screen_layer_iterator_t;
typedef struct surface_layer_iterator_s surface_layer_iterator_t;
typedef struct layer_defaults_s         layer_defaults_t;

struct mrp_glm_window_manager_s {
    MRP_WAYLAND_WINDOW_MANAGER_COMMON;
    application_t *app;
    ico_extension_t *ico;
    mrp_list_hook_t constructors;
    mrp_htbl_t *surfaces;
    mrp_htbl_t *layers;
    mrp_htbl_t *screens;
};

struct ctrl_surface_s {
    mrp_wayland_t *wl;
    struct ivi_controller_surface *ctrl_surface;
    struct wl_surface *wl_surface;
    int32_t nodeid;
    uint32_t id;
    int32_t pid;
    char *appid;
    char *title;
    int32_t x, y;
    int32_t width, height;
    int32_t requested_x, requested_y;
    int32_t requested_width, requested_height;
    double opacity;
    bool visible;
    int32_t layerid;
    mrp_wayland_window_t *win;
    struct {
        mrp_timer_t *title;
    } timer;
};

struct ctrl_layer_s {
    mrp_wayland_t *wl;
    struct ivi_controller_layer *ctrl_layer;
    int32_t id;
    char *name;
    struct wl_array surfaces;
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

struct ico_extension_s {
    MRP_WAYLAND_OBJECT_COMMON;
}; 

enum constructor_state_e {
    CONSTRUCTOR_FAILED = 0,     /* handle request failed */
    CONSTRUCTOR_INCOMPLETE,     /* pid is set but title is missing */
    CONSTRUCTOR_TITLED,         /* pid and title set */
    CONSTRUCTOR_SURFACELESS,    /* needs an ivi surface created by us */
    CONSTRUCTOR_REQUESTED,      /* native handle was requested */
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
    struct {
        mrp_timer_t *timeout;
        mrp_timer_t *surfaceless;
    } timer;
};

enum match_e {
    DOES_NOT_MATCH = 0,
    DOES_MATCH = 1
};

struct screen_layer_iterator_s {
    mrp_glm_window_manager_t *wm;
    mrp_wayland_output_t *out;
    ctrl_screen_t *scr;
    bool need_commit;
};

struct surface_layer_iterator_s {
    struct ivi_controller_layer *ctrl_layer;
    ctrl_layer_t *ly;
};

struct layer_defaults_s {
    int32_t zorder;
    double opacity;
    bool visibility;
};


static bool window_manager_constructor(mrp_wayland_t *,mrp_wayland_object_t *);
static void ctrl_screen_callback(void *, struct ivi_controller *, uint32_t,
                                 struct ivi_controller_screen *);
static void ctrl_layer_callback(void *, struct ivi_controller *, uint32_t);
static void ctrl_surface_callback(void *, struct ivi_controller *, uint32_t,
                                  int32_t, const char *);
static void ctrl_error_callback(void *, struct ivi_controller *, int32_t,
                                int32_t, int32_t, const char *);
static void ctrl_native_handle_callback(void *, struct ivi_controller *,
                                        struct wl_surface *);

static ctrl_surface_t *surface_create(mrp_glm_window_manager_t *,
                                      uint32_t, int32_t, const char *,
                                      struct wl_surface *);
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
                                 uint32_t, int32_t);
static bool surface_is_ready(mrp_glm_window_manager_t *, ctrl_surface_t *);
static void surface_set_title(mrp_glm_window_manager_t *, ctrl_surface_t *,
                              const char *);
static void surface_titleless(mrp_timer_t *, void *);


static ctrl_layer_t *layer_create(mrp_glm_window_manager_t *,
                                  mrp_wayland_layer_t *, ctrl_screen_t *);
#if 0
static void layer_destroy(mrp_glm_window_manager_t *, ctrl_layer_t *);
#endif
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
static bool layer_add_surface_to_top(ctrl_layer_t *, ctrl_surface_t *);
#if 0
static bool layer_add_surface_to_bottom(ctrl_layer_t *, ctrl_surface_t *);
#endif
static bool layer_move_surface_to_top(ctrl_layer_t *, ctrl_surface_t *);
static bool layer_move_surface_to_bottom(ctrl_layer_t *, ctrl_surface_t *);
static bool layer_remove_surface(ctrl_layer_t *, ctrl_surface_t *);
static void layer_send_surfaces(ctrl_layer_t *);


static uint32_t surface_id_generate(void);
static bool surface_id_is_ours(uint32_t);
static char *surface_id_print(uint32_t, char *, size_t);

static bool application_manager_constructor(mrp_wayland_t *,
                                            mrp_wayland_object_t *);
static void shell_info_callback(void *, struct ivi_application *,
                                int32_t, const char *, uint32_t);

static bool ico_extension_constructor(mrp_wayland_t *,mrp_wayland_object_t *);
static void ico_extension_window_active_callback(void*,struct ico_window_mgr*,
                                                 uint32_t, int32_t);
static void ico_extension_map_surface_callback(void *,struct ico_window_mgr *,
                                               int32_t, uint32_t, uint32_t,
                                               int32_t,int32_t, int32_t,
                                               uint32_t);
static void ico_extension_update_surface_callback(void *,
                                                  struct ico_window_mgr *,
                                                  uint32_t, int32_t,
                                                  int32_t, int32_t,
                                                  int32_t, int32_t,
                                                  int32_t, int32_t);
static void ico_extension_destroy_surface_callback(void *,
                                                   struct ico_window_mgr *,
                                                   uint32_t);


static constructor_t *constructor_create(mrp_glm_window_manager_t *,
                                         constructor_state_t,
                                         int32_t, const char *, uint32_t);
static void constructor_destroy(constructor_t *);
static void constructor_timeout(mrp_timer_t *, void *);
static void constructor_surfaceless(mrp_timer_t *, void *);
static constructor_t *constructor_find_first(mrp_glm_window_manager_t *,
                                             match_t,
                                             constructor_state_t);
static constructor_t *constructor_find_surface(mrp_glm_window_manager_t *,
                                               uint32_t,int32_t,const char *);

#if 0
static constructor_t *constructor_find_bound(mrp_glm_window_manager_t *,
                                             uint32_t);
#endif
static void constructor_set_title(mrp_glm_window_manager_t *,
                                  constructor_t *, const char *);
static void constructor_issue_next_request(mrp_glm_window_manager_t *);
static bool constructor_bind_ivi_surface(mrp_glm_window_manager_t *,
                                         constructor_t *);
static char *constructor_state_str(constructor_state_t);


static ctrl_layer_t *layer_find(mrp_glm_window_manager_t *, int32_t);
static void layer_request(mrp_wayland_layer_t *,
                          mrp_wayland_layer_update_t *);


static void window_request(mrp_wayland_window_t *,
                           mrp_wayland_window_update_t *,
                           mrp_wayland_animation_t *, uint32_t);


static void buffer_request(mrp_wayland_window_manager_t *, const char *,
                           uint32_t, uint32_t);

static uint32_t wid_hash(const void *);
static uint32_t sid_hash(const void *);
static uint32_t lid_hash(const void *);
static uint32_t oid_hash(const void *);
static int wid_compare(const void *, const void *);
static int id_compare(const void *, const void *);

static void get_appid(int32_t, char *, int);

static layer_defaults_t layer_defaults[MRP_WAYLAND_LAYER_TYPE_MAX] = {
    /*         layer type                  zorder opacity   visibility */
    /*---------------------------------------------------------------- */
    [ MRP_WAYLAND_LAYER_TYPE_UNKNOWN ] = {      2,  0.750,  HIDDEN     },
    [ MRP_WAYLAND_LAYER_BACKGROUND   ] = {      1,  0.000,  VISIBLE    },
    [ MRP_WAYLAND_LAYER_APPLICATION  ] = {      3,  1.000,  HIDDEN,    },
    [ MRP_WAYLAND_LAYER_INPUT        ] = {      4,  1.000,  HIDDEN     },
    [ MRP_WAYLAND_LAYER_TOUCH        ] = {      5,  1.000,  HIDDEN     },
    [ MRP_WAYLAND_LAYER_CURSOR       ] = {      6,  1.000,  HIDDEN     },
    [ MRP_WAYLAND_LAYER_STARTUP      ] = {      7,  1.000,  HIDDEN     },
    [ MRP_WAYLAND_LAYER_FULLSCREEN   ] = {      8,  1.000,  HIDDEN     },
};

static uint32_t    surface_hash_id = 1;
static mrp_htbl_t *surface_hash;


bool mrp_glm_window_manager_register(mrp_wayland_t *wl)
{
    mrp_htbl_config_t cfg;
    mrp_wayland_factory_t factory;

    memset(&cfg, 0, sizeof(cfg));
    cfg.nentry = MRP_WAYLAND_WINDOW_MAX;
    cfg.comp = wid_compare;
    cfg.hash = wid_hash;
    cfg.nbucket = MRP_WAYLAND_WINDOW_BUCKETS;

    if (!(surface_hash = mrp_htbl_create(&cfg))) {
        mrp_log_error("system-controller: can't create hash for surface IDs");
        return false;
    }

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

    factory.size = sizeof(ico_extension_t);
    factory.interface = &ico_window_mgr_interface;
    factory.constructor = ico_extension_constructor;
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
        .native_handle = ctrl_native_handle_callback
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

    MRP_ASSERT(wm->surfaces && wm->layers && wm->screens,
               "failed to create hash table");

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

static int screen_layer_iterator_cb(void *key, void *object, void *user_data)
{
    mrp_wayland_layer_t *layer = (mrp_wayland_layer_t *)object;
    screen_layer_iterator_t *it = (screen_layer_iterator_t *)user_data;

    MRP_UNUSED(key);

    if (!strcmp(layer->outputname, it->out->outputname)) {
        layer_create(it->wm, layer, it->scr);
        it->need_commit = true;
    }

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
    screen_layer_iterator_t it;

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
    it.need_commit = false;
    mrp_htbl_foreach(wl->layers.by_type, screen_layer_iterator_cb, &it);

    if (it.need_commit) {
        mrp_debug("calling ivi_controller_commit_changes()");
        ivi_controller_commit_changes((struct ivi_controller *)wm->proxy);
    }

    mrp_wayland_flush(wl);
}

static void ctrl_layer_callback(void *data,
                                struct ivi_controller *ivi_controller,
                                uint32_t id_layer)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)data;

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ivi_controller == (struct ivi_controller *)wm->proxy,
               "confused with data structures");

    mrp_debug("id_layer=%u", id_layer);
}

static void ctrl_surface_callback(void *data,
                                  struct ivi_controller *ivi_controller,
                                  uint32_t id_surface,
                                  int32_t pid,
                                  const char *title)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)data;
    constructor_t *c;
    struct wl_surface *wl_surface;
    char buf[256];

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ivi_controller == (struct ivi_controller *)wm->proxy,
               "confused with data structures");

    mrp_debug("id_surface=%s pid=%d title='%s'",
              surface_id_print(id_surface, buf,sizeof(buf)),
              pid, title ? title:"<null>");

    wl_surface = NULL;

    if ((c = constructor_find_surface(wm, id_surface, pid, title))) {
        switch (c->state) {

        case CONSTRUCTOR_BOUND:
            wl_surface = c->wl_surface;
            /* intentional fall over */

        case CONSTRUCTOR_TITLED:
        case CONSTRUCTOR_REQUESTED:
            if (!title || !title[0])
                title = c->title;
            break;

        default:
            break;
        }

        constructor_destroy(c);
    }

    surface_create(wm, id_surface, pid, title, wl_surface);
}


static void ctrl_error_callback(void *data,
                                struct ivi_controller *ivi_controller,
                                int32_t object_id,
                                int32_t object_type,
                                int32_t error_code,
                                const char *error_text)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)data;
    const char *type_str;

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ivi_controller == (struct ivi_controller *)wm->proxy,
               "confused with data structures");

    switch (object_type) {
    case IVI_CONTROLLER_OBJECT_TYPE_SURFACE:   type_str = "surface";     break;
    case IVI_CONTROLLER_OBJECT_TYPE_LAYER:     type_str = "layer";       break;
    case IVI_CONTROLLER_OBJECT_TYPE_SCREEN:    type_str = "screen";      break;
    default:                                   type_str = "<unknown>";   break;
    }

    if (!error_text)
        error_text = "???";

    mrp_debug("%s %d error %d: %s", type_str,object_id, error_code,error_text);

    switch (object_type) {

    case IVI_CONTROLLER_OBJECT_TYPE_SURFACE:
        if (error_code  == IVI_CONTROLLER_ERROR_CODE_NATIVE_HANDLE_END) {
            mrp_debug("native_handle_list end marker");
            constructor_issue_next_request(wm);
        }
        else {
            mrp_log_error("system-controller: surface error %d: %s",
                          error_code, error_text);
        }
        break;

    default:
        break;
    }
}

static void ctrl_native_handle_callback(void *data,
                                        struct ivi_controller *ivi_controller,
                                        struct wl_surface *wl_surface)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)data;
    application_t *app;
    constructor_t *reqsurf;

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ivi_controller == (struct ivi_controller *)wm->proxy,
               "confused with data structures");

    mrp_debug("wl_surface=%p", wl_surface);

    if (!(app = wm->app)) {
        mrp_log_error("system-controller: ivi application interface is down "
                      "when %s() is called", __FUNCTION__);
        return;
    }

    reqsurf = constructor_find_first(wm, DOES_MATCH, CONSTRUCTOR_REQUESTED);

    if (!reqsurf)
        mrp_debug("no pendig request");
    else {
        /* create an ivi-surface and bind it to wl_surface */
        reqsurf->id_surface = surface_id_generate();
        reqsurf->wl_surface = wl_surface;

        if (constructor_bind_ivi_surface(wm, reqsurf))
            reqsurf->state = CONSTRUCTOR_BOUND;
   }
}


static ctrl_surface_t *surface_create(mrp_glm_window_manager_t *wm,
                                      uint32_t id_surface,
                                      int32_t pid,
                                      const char *title,
                                      struct wl_surface *wl_surface)
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
    struct ivi_controller *ctrl;
    struct ivi_controller_surface *ctrl_surface;
    ctrl_surface_t *sf;
    void *data;
    char appid[1024];
    char id_str[256];

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");

    wl = wm->interface->wl;
    ctrl = (struct ivi_controller *)wm->proxy;
    sf = NULL;
    data = NULL + surface_hash_id++;

    surface_id_print(id_surface, id_str, sizeof(id_str));
    get_appid(pid, appid, sizeof(appid));

    mrp_debug("create surface "
              "(id=%s pid=%d appid='%s' title='%s' wl_surface=%p)",
              id_str, pid, appid, title ? title : "<null>", wl_surface);

    if (surface_find(wm, id_surface)) {
        mrp_log_error("system-controller: attempt to create multiple times "
                      "surface %s", id_str);
        return NULL;
    }

    if (!(ctrl_surface = ivi_controller_surface_create(ctrl, id_surface))) {
        mrp_log_error("system-controller: failed to create controller "
                      "surface for surface %s", id_str);
        return NULL;
    }

    mrp_debug("ctrl_surface %p was created for surface %s",
              ctrl_surface, id_str);

    if (surface_id_is_ours(id_surface)) {
        /* The ivi surface was created by us meaning we already had
           a surfaceless timeout. If title was not set during this
           time it is pointless to set another timeout to wait for
           the title. So, we set it to something to prevent furter
           delays */
        if (!title)
            title = "";
    }

    if (!(sf = mrp_allocz(sizeof(ctrl_surface_t)))) {
        mrp_log_error("system-controller: failed to allocate memory "
                      "for surface %s", id_str);
        goto failed;
    }

    sf->wl = wl;
    sf->ctrl_surface = ctrl_surface;
    sf->wl_surface = wl_surface;
    sf->id = id_surface;
    sf->pid = pid;
    sf->appid = mrp_strdup(appid);
    sf->title = title ? mrp_strdup(title) : NULL;
    sf->width = -1;
    sf->height = -1;
    sf->requested_x = MAX_COORDINATE+1;
    sf->requested_y = MAX_COORDINATE+1;
    sf->requested_width = -1;
    sf->requested_height = -1;
    sf->opacity = -1.0;
    sf->layerid = -1;

    if (!mrp_htbl_insert(wm->surfaces, &sf->id, sf)) {
        mrp_log_error("system-controller: hashmap by id: insertion error when "
                      "trying to create surface %s", id_str);
        goto failed;
    }

    if (!mrp_htbl_insert(surface_hash, data, sf)) {
        mrp_log_error("system-controller: hashmap by data insertion error when"
                      " trying to create surface %s", id_str);
        goto failed;
    }


    if (ivi_controller_surface_add_listener(ctrl_surface,&listener,data) < 0) {
        mrp_log_error("system-controller: failed to create surface %s "
                      "(can't listen to surface)", id_str);
        goto failed;
    }

    if (!sf->title) {
        sf->timer.title = mrp_add_timer(wl->ml, TITLE_TIMEOUT,
                                        surface_titleless, sf);
    }

    mrp_wayland_flush(wl);

    return sf;

  failed:
    if (sf) {
        mrp_htbl_remove(wm->surfaces, &sf->id, false);
        mrp_htbl_remove(surface_hash, data, false);
        mrp_free(sf->title);
        mrp_free(sf);
    }

    return NULL;
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
            mrp_del_timer(sf->timer.title);

            if (sf->win)
                mrp_wayland_window_destroy(sf->win);

            mrp_free(sf->appid);
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
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    mrp_wayland_window_update_t u;
    char buf[256];

    if (!(sf = mrp_htbl_lookup(surface_hash, data))) {
        mrp_log_error("system-controller: visibility callback "
                      "for non-existent surface");
        return;
    }

    MRP_ASSERT(sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) visibility=%d", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), visibility);

    sf->visible = visibility ? true : false;

    if (surface_is_ready(wm, sf)) {
        memset(&u, 0, sizeof(u));
        u.mask = MRP_WAYLAND_WINDOW_SURFACEID_MASK |
                 MRP_WAYLAND_WINDOW_VISIBLE_MASK;
        u.surfaceid = sf->id;
        u.visible = sf->visible;

        mrp_wayland_window_update(sf->win, MRP_WAYLAND_WINDOW_VISIBLE, &u);
    }
}

static void surface_opacity_callback(void *data,
                                 struct ivi_controller_surface *ctrl_surface,
                                 wl_fixed_t opacity)
{
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    mrp_wayland_window_update_t u;
    char buf[256];

    if (!(sf = mrp_htbl_lookup(surface_hash, data))) {
        mrp_log_error("system-controller: opacity callback "
                      "for non-existent surface");
        return;
    }

    MRP_ASSERT(sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");
    mrp_debug("ctrl_surface=%p (id=%s) opacity=%d", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), opacity);

    sf->opacity = wl_fixed_to_double(opacity);

    if (surface_is_ready(wm, sf)) {
        memset(&u, 0, sizeof(u));
        u.mask = MRP_WAYLAND_WINDOW_SURFACEID_MASK |
                 MRP_WAYLAND_WINDOW_OPACITY_MASK;
        u.surfaceid = sf->id;
        u.opacity = sf->opacity;

        mrp_wayland_window_update(sf->win, MRP_WAYLAND_WINDOW_VISIBLE, &u);
    }
}

static void surface_source_rectangle_callback(void *data,
                                   struct ivi_controller_surface *ctrl_surface,
				   int32_t x,
				   int32_t y,
				   int32_t width,
				   int32_t height)
{
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    if (!(sf = mrp_htbl_lookup(surface_hash, data))) {
        mrp_log_error("system-controller: source_rectangle callback "
                      "for non-existent surface");
        return;
    }

    MRP_ASSERT(sf->wl, "invalid argument");
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
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    mrp_wayland_window_update_t u;
    char buf[256];
    bool commit_needed = false;

    if (!(sf = mrp_htbl_lookup(surface_hash, data))) {
        mrp_log_error("system-controller: destination_rectangle callback "
                      "for non-existent surface");
        return;
    }

    MRP_ASSERT(sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) x=%d y=%d width=%d height=%d",
              ctrl_surface, surface_id_print(sf->id, buf, sizeof(buf)),
              x, y, width, height);

    sf->x = x;
    sf->y = y;
    sf->width = width;
    sf->height = height;

    if (surface_is_ready(wm, sf)) {
        /*
         * system-controller is the sole authority who manages the
         * destination rectangle. So if a rouge app is is fiddling
         * with it here we fight back and set it back what it was
         * requested.
         *
         * A quick series of legitime requests might result extra
         * wayland messages but hopefully will not end up in a infinite
         * loop ...
         */
        if ((sf->requested_x <= MAX_COORDINATE && x != sf->requested_x ) ||
            (sf->requested_y <= MAX_COORDINATE && y != sf->requested_y ) ||
            (width != sf->requested_width) || (height != sf->requested_height))
        {
            /*
             * If our original requested width/height are zero,
             * override them with the values from the surface's
             * window's area, if available.
             */
            if (!sf->requested_width || !sf->requested_height) {
                if (sf->win && sf->win->area) {
                    mrp_debug("overriding requested parameters: width %d -> %d, height %d -> %d",
                              sf->requested_width, sf->win->area->width,
                              sf->requested_height, sf->win->area->height);

                    sf->requested_width  = sf->win->area->width;
                    sf->requested_height = sf->win->area->height;
                }
            }

            /* Only rescale if width and height are nonzero */
            if (sf->requested_width && sf->requested_height) {
                mrp_debug("calling ivi_controller_surface_set_destination_"
                          "rectangle(ivi_controller_surface=%p, x=%d, y=%d, "
                          "width=%d height=%d)", sf->ctrl_surface,
                          sf->requested_x, sf->requested_y,
                          sf->requested_width, sf->requested_height);

                ivi_controller_surface_set_destination_rectangle(sf->ctrl_surface,
                                        sf->requested_x, sf->requested_y,
                                        sf->requested_width, sf->requested_height);

                commit_needed = true;
            }
        }

        memset(&u, 0, sizeof(u));
        u.mask      =  MRP_WAYLAND_WINDOW_SURFACEID_MASK |
                       MRP_WAYLAND_WINDOW_POSITION_MASK  |
                       MRP_WAYLAND_WINDOW_SIZE_MASK      ;
        u.surfaceid =  sf->id;
        u.x         =  sf->x;
        u.y         =  sf->y;
        u.width     =  sf->width;
        u.height    =  sf->height;

        mrp_wayland_window_update(sf->win, MRP_WAYLAND_WINDOW_CONFIGURE, &u);

        if (commit_needed) {
            mrp_debug("calling ivi_controller_commit_changes()");
            ivi_controller_commit_changes((struct ivi_controller *)wm->proxy);
        }

        mrp_wayland_flush(wl);
    }
}

static void surface_configuration_callback(void *data,
                                   struct ivi_controller_surface *ctrl_surface,
                                   int32_t width,
                                   int32_t height)
{
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    if (!(sf = mrp_htbl_lookup(surface_hash, data))) {
        mrp_log_error("system-controller: configuration callback "
                      "for non-existent surface");
        return;
    }

    MRP_ASSERT(sf->wl, "invalid argument");
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
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    if (!(sf = mrp_htbl_lookup(surface_hash, data))) {
        mrp_log_error("system-controller: orientation callback "
                      "for non-existent surface");
        return;
    }

    MRP_ASSERT(sf->wl, "invalid argument");
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
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    if (!(sf = mrp_htbl_lookup(surface_hash, data))) {
        mrp_log_error("system-controller: pixelformat callback "
                      "for non-existent surface");
        return;
    }

    MRP_ASSERT(sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) pixelformat=%d", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), pixelformat);
}

static int surface_layer_iterator_cb(void *key, void *object, void *user_data)
{
    ctrl_layer_t *ly = (ctrl_layer_t *)object;
    surface_layer_iterator_t *it = (surface_layer_iterator_t *)user_data;

    MRP_UNUSED(key);

    if (it->ctrl_layer == ly->ctrl_layer) {
        it->ly = ly;
        return MRP_HTBL_ITER_STOP;
    }

    return MRP_HTBL_ITER_MORE;
}

static void surface_added_to_layer_callback(void *data,
                                   struct ivi_controller_surface *ctrl_surface,
                                   struct ivi_controller_layer *ctrl_layer)
{
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    mrp_wayland_layer_t *layer;
    surface_layer_iterator_t it;
    ctrl_layer_t *ly;
    mrp_wayland_window_update_t u;
    char id_str[256];

    if (!(sf = mrp_htbl_lookup(surface_hash, data))) {
        mrp_log_error("system-controller: added_to_layer callback "
                      "for non-existent surface");
        return;
    }

    MRP_ASSERT(sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    surface_id_print(sf->id, id_str, sizeof(id_str));

    mrp_debug("ctrl_surface=%p (id=%s) ctrl_layer=%p", ctrl_surface,
              id_str, ctrl_layer);

    if (!ctrl_layer) {
        /* surface is removed from a layer */
#if 0   /* silently ignore the removal */
        sf->layerid = -1;
#endif
        return;  /* do not send notification of removal for the time being */
    }
    else {
        /* surface is added/moved to a layer */
        memset(&it, 0, sizeof(it));
        it.ctrl_layer = ctrl_layer;

        mrp_htbl_foreach(wm->layers, surface_layer_iterator_cb, &it);

        if (!(ly = it.ly)) {
            mrp_log_error("system-controller: can't update layer of surface %s"
                          "(ctrl_layer not found)", id_str);
            return;
        }

        if (!(layer = mrp_wayland_layer_find_by_id(wl, ly->id))) {
            mrp_log_error("system-controller: can't update layer of surface %s"
                          "(layer %d not found)", id_str, ly->id);
            return;
        }

        if (!surface_is_ready(wm, sf)) {
            mrp_log_error("system-controller: attempt to update layer "
                          "of non-ready surface %s", id_str);
            return;
        }
    }

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_WINDOW_SURFACEID_MASK | MRP_WAYLAND_WINDOW_LAYER_MASK;
    u.surfaceid = sf->id;
    u.layer = layer;

    mrp_wayland_window_update(sf->win, MRP_WAYLAND_WINDOW_CONFIGURE, &u);
}

static void surface_stats_callback(void *data,
                                   struct ivi_controller_surface *ctrl_surface,
                                   uint32_t redraw_count,
                                   uint32_t frame_count,
                                   uint32_t update_count,
                                   uint32_t pid,
                                   const char *process_name)
{
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    if (!(sf = mrp_htbl_lookup(surface_hash, data))) {
        mrp_log_error("system-controller: stats callback "
                      "for non-existent surface");
        return;
    }

    MRP_ASSERT(sf->wl, "invalid argument");
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

    if (sf->pid != (int32_t)pid) {
        mrp_debug("confused with data structures (mismatching pids)");
        return;
    }
}


static void surface_destroyed_callback(void *data,
                                   struct ivi_controller_surface *ctrl_surface)
{
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    ctrl_layer_t *ly;
    char buf[256];

    if (!(sf = mrp_htbl_remove(surface_hash, data, false))) {
        mrp_log_error("system-controller: attempt to destroy a nonexistent "
                      "surface");
        return;
    }

    MRP_ASSERT(sf && sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s)", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)));

    if (sf->layerid >= 0 && (ly = layer_find(wm, sf->layerid))) {
        layer_remove_surface(ly, sf);

        mrp_debug("calling ivi_controller_commit_changes()");
        ivi_controller_commit_changes((struct ivi_controller *)wm->proxy);

        mrp_wayland_flush(wl);
    }

    surface_destroy(wm, sf);
}

static void surface_content_callback(void *data,
                             struct ivi_controller_surface *ctrl_surface,
                             int32_t content_state)
{
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    if (!(sf = mrp_htbl_lookup(surface_hash, data))) {
        mrp_log_error("system-controller: content callback "
                      "for non-existent surface");
        return;
    }

    MRP_ASSERT(sf->wl, "invalid argument");
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
                                 uint32_t device,
                                 int32_t enabled)
{
    ctrl_surface_t *sf;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    if (!(sf = mrp_htbl_lookup(surface_hash, data))) {
        mrp_log_error("system-controller: input_focus callback "
                      "for non-existent surface");
        return;
    }

    MRP_ASSERT(sf->wl, "invalid argument");
    MRP_ASSERT(ctrl_surface == sf->ctrl_surface,
               "confused with data structures");

    wl = sf->wl;
    wm = (mrp_glm_window_manager_t *)wl->wm;

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("ctrl_surface=%p (id=%s) device=%u enabled=%d", ctrl_surface,
              surface_id_print(sf->id, buf, sizeof(buf)), device, enabled);
}

static bool surface_is_ready(mrp_glm_window_manager_t *wm, ctrl_surface_t *sf)
{
    mrp_wayland_t *wl;
    mrp_wayland_window_update_t u;
    bool ready;

    ready = sf->win ? true : false;

    if (!ready && wm->interface && wm->interface->wl) {
        wl = wm->interface->wl;

        if (sf->id     >  0 &&
            sf->pid    >  0 &&
            sf->appid       &&
            sf->title       &&
            sf->width  >= 0 &&
            sf->height >= 0  )
        {
            memset(&u, 0, sizeof(u));
            u.mask       =  MRP_WAYLAND_WINDOW_SURFACEID_MASK |
                            MRP_WAYLAND_WINDOW_NAME_MASK      |
                            MRP_WAYLAND_WINDOW_APPID_MASK     |
                            MRP_WAYLAND_WINDOW_PID_MASK       |
                            MRP_WAYLAND_WINDOW_NODEID_MASK    |
                            MRP_WAYLAND_WINDOW_POSITION_MASK  |
                            MRP_WAYLAND_WINDOW_SIZE_MASK      ;
            u.surfaceid  =  sf->id;
            u.name       =  sf->title;
            u.appid      =  sf->appid;
            u.pid        =  sf->pid;
            u.nodeid     =  sf->nodeid;
            u.x          =  sf->x;
            u.y          =  sf->y;
            u.width      =  sf->width;
            u.height     =  sf->height;

            sf->win = mrp_wayland_window_create(wl, &u);

            if (sf->visible || sf->opacity >= 0) {
                memset(&u, 0, sizeof(u));
                u.mask = MRP_WAYLAND_WINDOW_SURFACEID_MASK;
                u.surfaceid = sf->id;

                if (sf->visible) {
                    u.mask |= MRP_WAYLAND_WINDOW_VISIBLE_MASK;
                    u.visible = sf->visible;
                }

                if (sf->opacity >= 0) {
                    u.mask |= MRP_WAYLAND_WINDOW_OPACITY_MASK;
                    u.opacity = sf->opacity;
                }

                mrp_wayland_window_update(sf->win, MRP_WAYLAND_WINDOW_VISIBLE,
                                          &u);
            }
        }
    }

    return ready;
}

static void surface_set_title(mrp_glm_window_manager_t *wm,
                              ctrl_surface_t *sf,
                              const char *title)
{
    mrp_wayland_window_update_t u;

    mrp_debug("title='%s'", title);

    mrp_del_timer(sf->timer.title);
    sf->timer.title = NULL;

    if (sf->title && !strcmp(title, sf->title))
        mrp_debug("nothing to do (same title)");
    else {
        mrp_free(sf->title);
        sf->title = mrp_strdup(title ? title : "");

        if (surface_is_ready(wm, sf)) {
            memset(&u, 0, sizeof(u));
            u.mask      =  MRP_WAYLAND_WINDOW_SURFACEID_MASK |
                           MRP_WAYLAND_WINDOW_NAME_MASK;
            u.surfaceid =  sf->id;
            u.name      =  sf->title;

            mrp_wayland_window_update(sf->win,MRP_WAYLAND_WINDOW_NAMECHANGE,&u);
        }
    }
}

static void surface_titleless(mrp_timer_t *timer, void *user_data)
{
    ctrl_surface_t *sf = (ctrl_surface_t *)user_data;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(timer && sf && sf->wl && sf->wl->wm, "invalid argument");
    MRP_ASSERT(timer == sf->timer.title, "confused with data structures");

    wm = (mrp_glm_window_manager_t *)sf->wl->wm;

    mrp_debug("id=%u pid=%d appid='%s'", sf->id, sf->pid, sf->appid);

    mrp_del_timer(sf->timer.title);
    sf->timer.title = NULL;

    surface_set_title(wm, sf, "");
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
    layer_defaults_t *def;
    wl_fixed_t opacity;

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

    if (layer->type < 0 || layer->type >= MRP_WAYLAND_LAYER_TYPE_MAX)
        def = layer_defaults;
    else
        def = layer_defaults + layer->type;

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


    opacity = wl_fixed_from_double(def->opacity);
    mrp_debug("calling ivi_controller_layer_set_opacity"
              "(struct ivi_controller_layer=%p, opacity=%d)",
              ctrl_layer, opacity);
    ivi_controller_layer_set_opacity(ctrl_layer, opacity);


    mrp_debug("calling ivi_controller_layer_set_visibility"
              "(ivi_controller_layer=%p, visibility=%d)",
              ctrl_layer, def->visibility);
    ivi_controller_layer_set_visibility(ctrl_layer, def->visibility);


    return ly;
}

#if 0
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
            wl_array_release(&ly->surfaces);
            mrp_free(ly->name);
            mrp_free(ly);
        }
    }
}
#endif

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

#if 0
static bool layer_add_surface_to_bottom(ctrl_layer_t *ly, ctrl_surface_t *sf)
{
    uint32_t *last, *pos;
    char id_str[256];

    MRP_ASSERT(ly && sf, "invalid argument");
    MRP_ASSERT(ly->id == sf->layerid, "mismatching layer ID's");

    surface_id_print(sf->id, id_str, sizeof(id_str));

    wl_array_for_each(pos, &ly->surfaces) {
        if (*pos == (uint32_t)sf->id) {
            mrp_log_error("system-controller: can't add surface %s to the "
                          "bottom of layer %d (surface already exists)",
                          id_str, sf->layerid);
            return false;
        }
    }

    if (!(last = wl_array_add(&ly->surfaces, sizeof(uint32_t)))) {
        mrp_log_error("system-controller: can't add surface %s to the bottom "
                      "of layer %d (no memory)", id_str, sf->layerid);
        return false;
    }

    for (pos = last;   (void *)pos > ly->surfaces.data;  pos--)
        pos[0] = pos[-1];

    pos[0] = sf->id;

    mrp_debug("surface %s added to the bottom of layer %d",
              id_str, sf->layerid);

    layer_send_surfaces(ly);

    return true;
}
#endif

static bool layer_add_surface_to_top(ctrl_layer_t *ly, ctrl_surface_t *sf)
{
    uint32_t *last, *pos;
    char id_str[256];

    MRP_ASSERT(ly && sf, "invalid argument");
    MRP_ASSERT(ly->id == sf->layerid, "mismatching layer ID's");

    surface_id_print(sf->id, id_str, sizeof(id_str));

    wl_array_for_each(pos, &ly->surfaces) {
        if (*pos == (uint32_t)sf->id) {
            mrp_log_error("system-controller: can't add surface %s to the "
                          "top of layer %d (surface already exists)",
                          id_str, sf->layerid);
            return false;
        }
    }

    if (!(last = wl_array_add(&ly->surfaces, sizeof(uint32_t)))) {
        mrp_log_error("system-controller: can't add surface %s to the top "
                      "of layer %d (no memory)", id_str, sf->layerid);
        return false;
    }

    *last = sf->id;

    mrp_debug("surface %s added to the top of layer %d", id_str, sf->layerid);

    layer_send_surfaces(ly);

    return true;
}

static bool layer_move_surface_to_bottom(ctrl_layer_t *ly, ctrl_surface_t *sf)
{
    uint32_t *pos, *first, *last;
    int dim;
    bool removed;
    char id_str[256];

    MRP_ASSERT(ly && sf, "invalid argument");
    MRP_ASSERT(ly->id == sf->layerid, "mismatching layer ID's");

    surface_id_print(sf->id, id_str, sizeof(id_str));

    dim = ly->surfaces.size / sizeof(uint32_t);
    first = (uint32_t *)ly->surfaces.data;
    last = first + (dim - 1);

    removed = false;

    for (pos = last;  pos >= first;  pos--) {
        if (pos[0] == (uint32_t)sf->id)
            removed = true;
        else if (removed)
            pos[1] = pos[0];
    }

    if (!removed) {
        mrp_debug("failed to move surface %s to bottom of layer %d "
                  "(can't find surface)", id_str, sf->layerid);
        return false;
    }

    *first = (uint32_t)sf->id;

    mrp_debug("surface %s moved to bottom of layer %d", id_str, sf->layerid);

    layer_send_surfaces(ly);

    return true;
}

static bool layer_move_surface_to_top(ctrl_layer_t *ly, ctrl_surface_t *sf)
{
    uint32_t *pos, *first, *last;
    int dim;
    bool removed;
    char id_str[256];

    MRP_ASSERT(ly && sf, "invalid argument");
    MRP_ASSERT(ly->id == sf->layerid, "mismatching layer ID's");

    surface_id_print(sf->id, id_str, sizeof(id_str));

    dim = ly->surfaces.size / sizeof(uint32_t);
    first = (uint32_t *)ly->surfaces.data;
    last = first + (dim - 1);

    removed = false;

    wl_array_for_each(pos, &ly->surfaces) {
        if (pos[0] == (uint32_t)sf->id)
            removed = true;
        else if (removed)
            pos[-1] = pos[0];
    }

    if (!removed) {
        mrp_debug("failed to move surface %s to top of layer %d "
                  "(can't find surface)", id_str, sf->layerid);
        return false;
    }

    *last = (uint32_t)sf->id;

    mrp_debug("surface %s moved to top of layer %d", id_str, sf->layerid);

    layer_send_surfaces(ly);

    return true;
}

static bool layer_remove_surface(ctrl_layer_t *ly, ctrl_surface_t *sf)
{
    uint32_t *pos;
    bool removed;
    char id_str[256];

    MRP_ASSERT(ly && sf, "invalid argument");
    MRP_ASSERT(ly->id == sf->layerid, "mismatching layer ID's");

    surface_id_print(sf->id, id_str, sizeof(id_str));
    removed = false;

    wl_array_for_each(pos, &ly->surfaces) {
        if (pos[0] == (uint32_t)sf->id)
            removed = true;
        else if (removed)
            pos[-1] = pos[0];
    }

    if (!removed) {
        mrp_debug("failed to remove surface %s from layer %d "
                  "(can't find surface)", id_str, sf->layerid);
        return false;
    }

    ly->surfaces.size -= sizeof(uint32_t);

    mrp_debug("surface %s removed from layer %d", id_str, sf->layerid);

    mrp_debug("calling ivi_controller_layer_remove_surface"
              "(ivi_controller_layer=%p, surface=%p)",
              ly->ctrl_layer, sf->ctrl_surface);
    ivi_controller_layer_remove_surface(ly->ctrl_layer, sf->ctrl_surface);

    layer_send_surfaces(ly);

    return true;
}

static void layer_send_surfaces(ctrl_layer_t *ly)
{
    uint32_t *id_ptr;
    char *p, *e, *s;
    char buf[8192];

    e = (p = buf) + sizeof(buf);
    s = "";

    wl_array_for_each(id_ptr, &ly->surfaces) {
        if (p >= e)
            break;
        else {
            p += snprintf(p, e-p, "%s%u", s, *id_ptr);
            s  = ", ";
        }
    }

    mrp_debug("calling ivi_controller_layer_set_render_order"
              "(ivi_controller_layer=%p, id_surfaces=[%s])",
              ly->ctrl_layer, buf);

    ivi_controller_layer_set_render_order(ly->ctrl_layer, &ly->surfaces);
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
                                const char *title,
                                uint32_t id_surface)
{
    application_t *app = (application_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    ctrl_surface_t *sf;
    mrp_list_hook_t *c, *n;
    constructor_t *entry, *found;
    constructor_state_t state;
    bool has_title;
    char buf[256];

    MRP_ASSERT(pid > 0 && title, "invalid argument");
    MRP_ASSERT(app && app->interface && app->interface->wl,"invalid argument");
    MRP_ASSERT(ivi_application == (struct ivi_application *)app->proxy,
               "confused with data structures");

    wl = app->interface->wl;

    mrp_debug("pid=%d title='%s' id_surface=%s",
              pid, title ? title : "<null>",
              surface_id_print(id_surface, buf, sizeof(buf)));

    if (!(wm = (mrp_glm_window_manager_t *)wl->wm)) {
        mrp_debug("controller interface is not ready");
        return;
    }

    has_title = (title && title[0]);

    if (!id_surface) {
        /* no surface ID */

        if (!has_title)
            constructor_create(wm, CONSTRUCTOR_INCOMPLETE, pid, NULL, 0);
        else {
            /* try to find an incomplete constructor with a matching pid */
            found = NULL;

            mrp_list_foreach(&wm->constructors, c, n) {
                entry = mrp_list_entry(c, constructor_t, link);

                if (pid == entry->pid) {
                    if ((entry->state == CONSTRUCTOR_INCOMPLETE) ||
                        (entry->state == CONSTRUCTOR_TITLED &&
                         !strcmp(title, entry->title)          )  )
                    {
                        found = entry;
                        break;
                    }
                }
            }

            if (!found)
                constructor_create(wm, CONSTRUCTOR_TITLED, pid, title, 0);
            else
                constructor_set_title(wm, found, title);
        }
    }
    else {
        /* we have a surface ID */

        if (!(sf = surface_find(wm, id_surface))) {
            /* no surface found with the ID */

            /* try to find a matching constructor */
            found = constructor_find_surface(wm, id_surface, pid, title);

            if (!found) {
                if (has_title)
                    state = CONSTRUCTOR_TITLED;
                else
                    state = CONSTRUCTOR_INCOMPLETE;

                constructor_create(wm, state, pid, title, id_surface);
            }
            else {
                if (id_surface > 0) {
                    /* found a constructor with the ID */
                    if (pid != found->pid) {
                        mrp_log_error("system-controller: confused with "
                                      "surface constructors "
                                      "(mismatching PIDs: %d vs. %d)",
                                      pid, found->pid);
                    }
                    else {
                        if (has_title)
                            constructor_set_title(wm, found, title);
                    }
                }
                else {
                    mrp_debug("found a constructor with matching pid/title. "
                              "Updating it ...");

                    found->id_surface = id_surface;

                    if (found->timer.surfaceless) {
                        mrp_del_timer(found->timer.surfaceless);
                        found->timer.surfaceless = NULL;
                    }

                    if (has_title)
                        constructor_set_title(wm, found, title);
                }
            }
        }
        else {
            /* found a surface with the ID */
            if (pid != sf->pid) {
                mrp_log_error("system-controller: confused with control "
                              "surfaces (mismatching PIDs: %d vs. %d)",
                              pid, sf->pid);
            }
            else {
                if (has_title)
                    surface_set_title(wm, sf, title);
            }
        }
    }
}


static bool ico_extension_constructor(mrp_wayland_t *wl,
                                      mrp_wayland_object_t *obj)
{
    static struct ico_window_mgr_listener listener =  {
        .window_active   = ico_extension_window_active_callback,
        .map_surface     = ico_extension_map_surface_callback,
        .update_surface  = ico_extension_update_surface_callback,
        .destroy_surface = ico_extension_destroy_surface_callback
    };

    ico_extension_t *ico = (ico_extension_t *)obj;
    mrp_glm_window_manager_t *wm;
    int sts;

    if (!(wm = (mrp_glm_window_manager_t *)(wl->wm))) {
        mrp_log_error("system-controller: phase error in %s(): "
                      "window manager does not exist", __FUNCTION__);
        return false;
    }

    sts = ico_window_mgr_add_listener((struct ico_window_mgr *)ico->proxy,
                                      &listener, ico);
    if (sts < 0)
        return false;

    wm->ico = ico;

    return true;
}


static void ico_extension_window_active_callback(void *data,
			      struct ico_window_mgr *ico_window_mgr,
			      uint32_t surfaceid,
			      int32_t select)
{
    ico_extension_t *ico = (ico_extension_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    ctrl_surface_t *sf;
    mrp_wayland_window_t *win;
    mrp_wayland_window_update_t u;
    char id_str[256];

    MRP_ASSERT(ico && ico->interface && ico->interface->wl,
               "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)ico->proxy,
               "confused with data structures");

    wl = ico->interface->wl;
    wm = (mrp_glm_window_manager_t *)(wl->wm);

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("surfaceid=%u select=%d", surfaceid, select);

    if (!select) {
        mrp_debug("ignoring select=0 events");
        return;
    }

    surface_id_print(surfaceid, id_str, sizeof(id_str));

    if (!(sf = surface_find(wm, surfaceid))) {
        mrp_debug("can't find surface for id=%s", id_str);
        return;
    }

    if (!(win = sf->win)) {
        mrp_debug("can't forward selection event for surface id=%s "
                  "(no window)", id_str);
        return;
    }

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_WINDOW_ACTIVE_MASK;
    u.active = 0;

    if ((select & ICO_WINDOW_MGR_SELECT_POINTER))
        u.active |= MRP_WAYLAND_WINDOW_ACTIVE_POINTER;
    if ((select & ICO_WINDOW_MGR_SELECT_TOUCH))
        u.active |= MRP_WAYLAND_WINDOW_ACTIVE_TOUCH;

    mrp_wayland_window_update(win, MRP_WAYLAND_WINDOW_ACTIVE, &u);
}


static void ico_extension_map_surface_callback(void *data,
			      struct ico_window_mgr *ico_window_mgr,
                              int32_t event,
			      uint32_t surfaceid,
			      uint32_t type,
			      int32_t width,
			      int32_t height,
			      int32_t stride,
			      uint32_t format)
{
    ico_extension_t *ico = (ico_extension_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    ctrl_surface_t *sf;
    mrp_wayland_window_t *win;
    mrp_wayland_window_update_t u;
    mrp_wayland_window_map_t map;
    char id_str[256];

    MRP_ASSERT(ico && ico->interface && ico->interface->wl,
               "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)ico->proxy,
               "confused with data structures");

    wl = ico->interface->wl;
    wm = (mrp_glm_window_manager_t *)(wl->wm);

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("event=%d surfaceid=%u type=%u width=%d height=%d stride=%d "
              "format=%u",
              event, surfaceid, type, width,height, stride, format);

    surface_id_print(surfaceid, id_str, sizeof(id_str));

    if (!(sf = surface_find(wm, surfaceid))) {
        mrp_debug("can't find surface for id=%s", id_str);
        return;
    }

    if (!(win = sf->win)) {
        mrp_debug("can't forward map event for surface id=%s "
                  "(no window)", id_str);
        return;
    }

    memset(&map, 0, sizeof(map));
    map.type = type;
    map.width = width;
    map.height = height;
    map.stride = stride;
    map.format = format;

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_WINDOW_MAPPED_MASK | MRP_WAYLAND_WINDOW_MAP_MASK;
    u.map  = &map;

    switch (event)  {

    case ICO_WINDOW_MGR_MAP_SURFACE_EVENT_CONTENTS:
    case ICO_WINDOW_MGR_MAP_SURFACE_EVENT_RESIZE:
    case ICO_WINDOW_MGR_MAP_SURFACE_EVENT_MAP:
        u.mapped = true;
        break;

    case ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP:
    case ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR:
        u.mapped = false;
        break;

    default:
        mrp_debug("ignoring unknown event type %d event", event);
        return;
    }

    mrp_wayland_window_update(win, MRP_WAYLAND_WINDOW_MAP, &u);
}

static void ico_extension_update_surface_callback(void *data,
			      struct ico_window_mgr *ico_window_mgr,
			      uint32_t surfaceid,
			      int32_t visible,
			      int32_t srcwidth,
			      int32_t srcheight,
			      int32_t x,
			      int32_t y,
			      int32_t width,
			      int32_t height)
{
    ico_extension_t *ico = (ico_extension_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(ico && ico->interface && ico->interface->wl,
               "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)ico->proxy,
               "confused with data structures");

    wl = ico->interface->wl;
    wm = (mrp_glm_window_manager_t *)(wl->wm);

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("surfaceid=%u visible=%d srcwidth=%d srcheight=%d x=%d y=%d "
              "width=%d height=%d",
              surfaceid, visible, srcwidth,srcheight, x,y, width,height);
}


static void ico_extension_destroy_surface_callback(void *data,
			       struct ico_window_mgr *ico_window_mgr,
			       uint32_t surfaceid)
{
    ico_extension_t *ico = (ico_extension_t *)data;
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(ico && ico->interface && ico->interface->wl,
               "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)ico->proxy,
               "confused with data structures");

    wl = ico->interface->wl;
    wm = (mrp_glm_window_manager_t *)(wl->wm);

    MRP_ASSERT(wm, "data inconsitency");

    mrp_debug("surfaceid=%u", surfaceid);
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
    return ((id & SURFACE_ID_OUR_FLAG) == SURFACE_ID_OUR_FLAG) ? true : false;
}

static char *surface_id_print(uint32_t id, char *buf, size_t len)
{
    if (!id)
        snprintf(buf, len, "<not set>");
    else {
        snprintf(buf, len, "%u(%s-id-%u)", id,
                 surface_id_is_ours(id) ? "our":"native",
                 (id & SURFACE_ID_MAX));
    }

    return buf;
}

static constructor_t *constructor_create(mrp_glm_window_manager_t *wm,
                                         constructor_state_t state,
                                         int32_t pid,
                                         const char *title,
                                         uint32_t id_surface)
{
    mrp_wayland_t *wl;
    constructor_t *c;
    char buf[256];

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");

    wl = wm->interface->wl;

    if (!(c = mrp_allocz(sizeof(constructor_t)))) {
        mrp_log_error("system-controller: can't allocate memory for surface"
                      "(pid=%d title='%s' id_surface=%s)",
                      pid, title ? title : "<null>",
                      surface_id_print(id_surface, buf, sizeof(buf)));
        return NULL;
    }

    mrp_list_init(&c->link);
    c->wm = wm;
    c->state = state;
    c->pid = pid;
    c->title = title ? mrp_strdup(title) : NULL;
    c->id_surface = id_surface;
    c->timer.timeout = mrp_add_timer(wl->ml, CONSTRUCTOR_TIMEOUT,
                                     constructor_timeout, c);

    if (!id_surface) {
        c->timer.surfaceless = mrp_add_timer(wl->ml, SURFACELESS_TIMEOUT,
                                             constructor_surfaceless, c);
    }

    mrp_list_append(&wm->constructors, &c->link);

    mrp_debug("constructor created (state=%s, pid=%d, title='%s' "
              "id_surface=%u)", constructor_state_str(state), pid,
              title ? title: "<null>", id_surface);

    return c;
}

static void constructor_destroy(constructor_t *c)
{
    char buf[256];

    if (c) {
        mrp_debug("surface %s of application (pid=%d) destroyed",
                  surface_id_print(c->id_surface, buf, sizeof(buf)), c->pid);

        mrp_del_timer(c->timer.timeout);
        mrp_del_timer(c->timer.surfaceless);

        mrp_list_delete(&c->link);
        mrp_free(c->title);

        mrp_free(c);
    }
}

static void constructor_timeout(mrp_timer_t *timer, void *user_data)
{
    constructor_t *c = (constructor_t *)user_data;
    mrp_glm_window_manager_t *wm;
    char buf[256];

    MRP_ASSERT(timer && c && c->wm, "invalid argument");
    MRP_ASSERT(timer == c->timer.timeout, "confused with data structures");

    mrp_debug("pid=%d title='%s' id_surface=%s state=%s",
              c->pid, c->title ? c->title : "",
              surface_id_print(c->id_surface, buf, sizeof(buf)),
              constructor_state_str(c->state));

    wm = c->wm;

    constructor_destroy(c);
    constructor_issue_next_request(wm);
}

static void constructor_surfaceless(mrp_timer_t *timer, void *user_data)
{
    constructor_t *c = (constructor_t *)user_data;
    mrp_glm_window_manager_t *wm;

    MRP_ASSERT(timer && c && c->wm, "invalid argument");
    MRP_ASSERT(timer == c->timer.surfaceless, "confused with data structures");

    mrp_debug("pid=%d title='%s' state=%s",
              c->pid, c->title ? c->title : "<not set>",
              constructor_state_str(c->state));

    wm = c->wm;

    mrp_del_timer(c->timer.surfaceless);
    c->timer.surfaceless = NULL;

    c->state = CONSTRUCTOR_SURFACELESS;
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

static constructor_t *constructor_find_surface(mrp_glm_window_manager_t *wm,
                                               uint32_t id_surface,
                                               int32_t pid,
                                               const char *title)
{
    mrp_list_hook_t *c, *n;
    constructor_t *entry, *candidate;
    bool candidate_titleless;

    candidate = NULL;
    candidate_titleless = false;

    mrp_list_foreach(&wm->constructors, c, n) {
        entry = mrp_list_entry(c, constructor_t, link);

        if (id_surface > 0) {
            if (id_surface == entry->id_surface)
                return entry;
        }

        if (pid == entry->pid) {
            if (entry->state == CONSTRUCTOR_INCOMPLETE) {
                if (!title && !candidate)
                    candidate = entry;
            }
            else {
                if (title && !strcmp(title, entry->title)) {
                    if (!candidate || !candidate_titleless) {
                        candidate = entry;
                        candidate_titleless = false;
                    }
                }
            }
        }
    }

    return candidate;
}

#if 0
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
#endif

static void constructor_set_title(mrp_glm_window_manager_t *wm,
                                  constructor_t *c,
                                  const char *title)
{
    (void)wm;

    if (title && title[0]) {
        mrp_free((void *)c->title);
        c->title = title ? mrp_strdup(title) : NULL;

        mrp_debug("constructor title changed to '%s'", c->title);

        if (c->state == CONSTRUCTOR_INCOMPLETE) {
            c->state = CONSTRUCTOR_TITLED;
            mrp_debug("constructor state changed to %s",
                      constructor_state_str(c->state));
        }
    }
}

static void constructor_issue_next_request(mrp_glm_window_manager_t *wm)
{
    mrp_list_hook_t *c, *n;
    constructor_t *entry;

    mrp_debug("loop through constructors ...");

    mrp_list_foreach(&wm->constructors, c, n) {
        entry = mrp_list_entry(c, constructor_t, link);

        mrp_debug("   state %s", constructor_state_str(entry->state));

        if (entry->state == CONSTRUCTOR_SURFACELESS) {
            mrp_debug("      call ivi_controller_get_native_handle"
                      "(pid=%d title='%s')", entry->pid,
                      entry->title ? entry->title : "<not set>");

            ivi_controller_get_native_handle((struct ivi_controller*)wm->proxy,
                                             entry->pid, entry->title);

            entry->state = CONSTRUCTOR_REQUESTED;

            mrp_wayland_flush(wm->interface->wl);

            return;
        }

        if (entry->state == CONSTRUCTOR_REQUESTED)
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
                      "(id_surface is not set)");
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
    case CONSTRUCTOR_SURFACELESS: return "surfaceless";
    case CONSTRUCTOR_REQUESTED:   return "requested";
    case CONSTRUCTOR_BOUND:       return "bound";
    default:                      return "<unknown>";
    }
}

static ctrl_layer_t *layer_find(mrp_glm_window_manager_t *wm, int32_t id_layer)
{
    ctrl_layer_t *ly;

    if (!wm || !wm->layers)
        ly = NULL;
    else
        ly = mrp_htbl_lookup(wm->layers, &id_layer);

    return ly;
}

static bool set_layer_visibility(mrp_wayland_layer_t *layer,
                                 mrp_wayland_layer_update_mask_t passthrough,
                                 mrp_wayland_layer_update_t *u)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)layer->wm;
    ctrl_layer_t *ly;
    uint32_t visibility;

    (void)passthrough;

    if (!(ly = layer_find(wm, layer->layerid))) {
        mrp_debug("can't find layer");
        return false;
    }

    visibility = u->visible ? 1 : 0;

    mrp_debug("call ivi_controller_layer_set_visibility"
              "(ivi_controller_layer=%p, visibility=%u)",
              ly->ctrl_layer, visibility);

    ivi_controller_layer_set_visibility(ly->ctrl_layer, visibility);

    return true;
}

static void layer_request(mrp_wayland_layer_t *layer,
                          mrp_wayland_layer_update_t *u)
{
    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    mrp_wayland_layer_update_mask_t mask;
    mrp_wayland_layer_update_mask_t passthrough;
    bool changed;
    char buf[2048];

    MRP_ASSERT(layer && layer->wm && layer->wm->proxy &&
               layer->wm->interface && layer->wm->interface->wl,
               "invalid argument");

    wm = (mrp_glm_window_manager_t *)layer->wm;
    wl = wm->interface->wl;
    passthrough = wm->passthrough.layer_request;
    mask = u->mask;
    changed = false;

    mrp_wayland_layer_request_print(u, buf, sizeof(buf));
    mrp_debug("request for layer %d update:%s", layer->layerid, buf);

    while (mask) {
        if ((mask & MRP_WAYLAND_LAYER_VISIBLE_MASK)) {
            changed |= set_layer_visibility(layer, passthrough, u);
            mask &= ~MRP_WAYLAND_LAYER_VISIBLE_MASK;
        }
        else {
            mask = 0;
        }
    }


    if (changed) {
        mrp_debug("calling ivi_controller_commit_changes()");
        ivi_controller_commit_changes((struct ivi_controller *)wm->proxy);
    }

    mrp_wayland_flush(wl);
}


static void set_window_animation(mrp_wayland_window_t *win,
                                 mrp_wayland_animation_type_t type,
                                 mrp_wayland_animation_t *anims)
{
    static int32_t ico_types[MRP_WAYLAND_ANIMATION_MAX] = {
        [MRP_WAYLAND_ANIMATION_HIDE]   = ICO_WINDOW_MGR_ANIMATION_TYPE_HIDE,
        [MRP_WAYLAND_ANIMATION_SHOW]   = ICO_WINDOW_MGR_ANIMATION_TYPE_SHOW,
        [MRP_WAYLAND_ANIMATION_MOVE]   = ICO_WINDOW_MGR_ANIMATION_TYPE_MOVE,
        [MRP_WAYLAND_ANIMATION_RESIZE] = ICO_WINDOW_MGR_ANIMATION_TYPE_RESIZE
    };

    mrp_glm_window_manager_t *wm;
    ico_extension_t *ico;
    struct ico_window_mgr *ico_window_mgr;
    mrp_wayland_animation_t *a;

    MRP_ASSERT(win && win->wm, "invalid argument");

    wm = (mrp_glm_window_manager_t *)(win->wm);
    ico = wm->ico;

    if (!ico) {
        mrp_debug("can't animate on window %u (ico-extension not available)",
                  win->surfaceid);
        return;
    }

    ico_window_mgr = (struct ico_window_mgr *)ico->proxy;

    if (anims && type >= 0 && type < MRP_WAYLAND_ANIMATION_MAX) {
        a = anims + type;

        if (a->name && a->name[0] && a->time > 0) {
            mrp_debug("calling ico_window_mgr_set_animation"
                      "(surfaceid=%d type=%d, animation='%s' time=%d)",
                      win->surfaceid, ico_types[type], a->name, a->time);

            ico_window_mgr_set_animation(ico_window_mgr, win->surfaceid,
                                         ico_types[type], a->name, a->time);
        }
    }
}

static bool set_window_layer(mrp_wayland_window_t *win,
                             mrp_wayland_window_update_mask_t passthrough,
                             mrp_wayland_window_update_t *u)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)win->wm;
    int32_t id_surface;
    int32_t id_layer;
    ctrl_surface_t *sf;
    ctrl_layer_t *ly;
    bool changed;
    mrp_wayland_window_update_t u2;
    char buf[256];

    (void)passthrough;

    if (!u->layer) {
        mrp_log_error("system-controller: broken request (layer is <null>)");
        return false;
    }

    id_surface = win->surfaceid;
    id_layer = u->layer->layerid;

    if (!(sf = surface_find(wm, id_surface))) {
        mrp_debug("can't find surface %s",
                  surface_id_print(id_surface, buf, sizeof(buf)));
        return false;
    }

    if (!(ly = layer_find(wm, id_layer))) {
        mrp_debug("can't find layer %d", id_layer);
        return false;
    }

    if (sf->layerid == id_layer) {
        if ((u->mask & MRP_WAYLAND_WINDOW_RAISE_MASK))
            changed = false;
        else
            changed = layer_move_surface_to_top(ly, sf);
    }
    else {
        sf->layerid = id_layer;
        changed = layer_add_surface_to_top(ly, sf);
    }

    if (changed) {
        memset(&u2, 0, sizeof(u2));
        u2.mask = MRP_WAYLAND_WINDOW_SURFACEID_MASK |
                  MRP_WAYLAND_WINDOW_LAYER_MASK |
                  MRP_WAYLAND_WINDOW_RAISE_MASK;
        u2.surfaceid = win->surfaceid;
        u2.layer = u->layer;
        u2.raise = 1;

        mrp_debug("calling mrp_wayland_window_update(surface=%s, layer=%d,"
                  "raise=%d)", surface_id_print(u2.surfaceid,
                                                buf, sizeof(buf)),
                  u2.layer->layerid, u2.raise);

        mrp_wayland_window_update(win, MRP_WAYLAND_WINDOW_VISIBLE, &u2);
    }

    return changed;
}

static bool set_window_mapped(mrp_wayland_window_t *win,
                              mrp_wayland_window_update_mask_t passthrough,
                              mrp_wayland_window_update_t *u,
                              mrp_wayland_animation_t *anims,
                              uint32_t framerate)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)win->wm;
    ico_extension_t *ico = wm->ico;
    int32_t id_surface;
    struct ico_window_mgr *ico_window_mgr;
    const char *filepath;
    char id_str[256];

    ico_window_mgr = ico ? (struct ico_window_mgr *)ico->proxy : NULL;
    id_surface = win->surfaceid;

    surface_id_print(id_surface, id_str, sizeof(id_str));

    if (((u->mapped && win->mapped) || (!u->mapped && !win->mapped)) &&
        !(passthrough & MRP_WAYLAND_WINDOW_MAPPED_MASK))
    {
        mrp_debug("nothing to do");
        return false;
    }

    if (u->mapped) {
        if (!anims || !(filepath = anims[MRP_WAYLAND_ANIMATION_MAP].name)) {
            mrp_log_error("system-controller: broken map request "
                          "(no file path)");
            return false;
        }

        if (!ico_window_mgr) {
            mrp_debug("can't map surface %s to file '%s' (ico-extension not "
                      "available)", id_str, filepath);
            return false;
        }

        if (framerate == 0)
            framerate = -1;

        mrp_debug("calling ico_window_mgr_map_surface"
                  "(ico_window_mgr=%p, surfaceid=%u, framerate=%d, "
                  "filepath='%s')",
                  ico_window_mgr, id_surface, framerate, filepath);

        ico_window_mgr_map_surface(ico_window_mgr, id_surface, framerate,
                                   filepath);
    }
    else {
        if (!ico_window_mgr) {
            mrp_debug("can't unmap surface %s (ico-extension not available)",
                      id_str);
            return false;
        }

        mrp_debug("calling ico_window_mgr_unmap_surface"
                  "(ico_window_mgr=%p, surfaceid=%u)",
                  ico_window_mgr, id_surface);

        ico_window_mgr_unmap_surface(ico_window_mgr, id_surface);
    }

    return true;
}

static bool set_window_geometry(mrp_wayland_window_t *win,
                                mrp_wayland_window_update_mask_t passthrough,
                                mrp_wayland_window_update_t *u,
                                mrp_wayland_animation_t *anims)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)win->wm;
    mrp_wayland_window_update_mask_t mask = u->mask;
    int32_t id_surface = win->surfaceid;
    bool output_changed = false;
    ctrl_surface_t *sf;
    int32_t x,y;
    int32_t w,h;
    char buf[256];

    (void)passthrough;

    if (!output_changed                                        &&
        (!(mask & MRP_WAYLAND_WINDOW_POSITION_MASK) ||
         (u->x == win->x && u->y == win->y)                  ) &&
        (!(mask & MRP_WAYLAND_WINDOW_SIZE_MASK) ||
         (u->width == win->width && u->height == win->height))  )
    {
        mrp_debug("nothing to do");
        return false;
    }

    if (!(sf = surface_find(wm, id_surface))) {
        mrp_debug("can't find surface %s",
                  surface_id_print(id_surface, buf, sizeof(buf)));
        return false;
    }

    x = (mask & MRP_WAYLAND_WINDOW_X_MASK     )  ?  u->x      : win->x;
    y = (mask & MRP_WAYLAND_WINDOW_Y_MASK     )  ?  u->y      : win->y;
    w = (mask & MRP_WAYLAND_WINDOW_WIDTH_MASK )  ?  u->width  : win->width;
    h = (mask & MRP_WAYLAND_WINDOW_HEIGHT_MASK)  ?  u->height : win->height;

    if (x != win->x || y != win->y)
        set_window_animation(win, MRP_WAYLAND_ANIMATION_MOVE, anims);
    if (w != win->width || h != win->height)
        set_window_animation(win, MRP_WAYLAND_ANIMATION_RESIZE, anims);

#if 0
    mrp_debug("calling ivi_controller_surface_set_source_rectangle"
              "(ivi_controller_surface=%p, x=0, y=0, width=%d height=%d)",
              sf->ctrl_surface, w,h);

    ivi_controller_surface_set_source_rectangle(sf->ctrl_surface, 0,0, w,h);
#endif


    sf->requested_x = x;
    sf->requested_y = y;
    sf->requested_width = w;
    sf->requested_height = h;

    mrp_debug("calling ivi_controller_surface_set_destination_rectangle"
              "(ivi_controller_surface=%p, x=%d, y=%d, width=%d height=%d)",
              sf->ctrl_surface, x,y, w,h);

    ivi_controller_surface_set_destination_rectangle(sf->ctrl_surface,
                                                     x,y, w,h);

    return true;
}


static bool set_window_opacity(mrp_wayland_window_t *win,
                               mrp_wayland_window_update_mask_t passthrough,
                               mrp_wayland_window_update_t *u)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)win->wm;
    int32_t id_surface = win->surfaceid;
    ctrl_surface_t *sf;
    wl_fixed_t opacity;
    char buf[256];

    if (u->opacity == win->opacity &&
        !(passthrough & MRP_WAYLAND_WINDOW_OPACITY_MASK))
    {
        mrp_debug("nothing to do");
        return false;
    }

    if (!(sf = surface_find(wm, id_surface))) {
        mrp_debug("can't find surface %s",
                  surface_id_print(id_surface, buf, sizeof(buf)));
        return false;
    }
    
    opacity = wl_fixed_from_double(u->opacity);

    mrp_debug("calling ivi_controller_surface_set_opacity"
              "(ivi_controller_surface=%p, opacity=%d)",
              sf->ctrl_surface, opacity);

    ivi_controller_surface_set_opacity(sf->ctrl_surface, opacity);

    return true;
}


static bool raise_window(mrp_wayland_window_t *win,
                         mrp_wayland_window_update_mask_t passthrough,
                         mrp_wayland_window_update_t *u)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)win->wm;
    mrp_wayland_window_update_t u2;
    int32_t id_surface = win->surfaceid;
    ctrl_surface_t *sf;
    ctrl_layer_t *ly;
    bool changed;
    char buf[256];

    (void)passthrough;

    do { /* not a loop */
        changed = false;

        if (((u->raise && win->raise) || (!u->raise && !win->raise))) {
            mrp_debug("no actual change is needed");
            break;
        }

        if (!(sf = surface_find(wm, win->surfaceid))) {
            mrp_debug("can't find surface %s",
                      surface_id_print(id_surface, buf, sizeof(buf)));
            return false;
        }

        if (!win->layer) {
            mrp_debug("layer is not set");
            return false;
        }

        if (!(ly = layer_find(wm, win->layer->layerid))) {
            mrp_debug("can't find layer %d", win->layer->layerid);
            return false;
        }

        if (u->raise)
            changed = layer_move_surface_to_top(ly, sf);
        else
            changed = layer_move_surface_to_bottom(ly, sf);

    } while (0);


    memset(&u2, 0, sizeof(u2));
    u2.mask = MRP_WAYLAND_WINDOW_SURFACEID_MASK |
              MRP_WAYLAND_WINDOW_RAISE_MASK;
    u2.surfaceid = win->surfaceid;
    u2.raise = u->raise;
    
    mrp_wayland_window_update(win, MRP_WAYLAND_WINDOW_VISIBLE, &u2);

    return changed;
}


static bool set_window_visibility(mrp_wayland_window_t *win,
                                 mrp_wayland_window_update_mask_t passthrough,
                                 mrp_wayland_window_update_t *u,
                                 mrp_wayland_animation_t *anims)
{
    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)win->wm;
    int32_t id_surface;
    uint32_t visibility;
    ctrl_surface_t *sf;
    mrp_wayland_animation_type_t anim_type;
    char buf[256];

    if ((((u->visible && win->visible) || (!u->visible && !win->visible)) &&
         !(passthrough & MRP_WAYLAND_WINDOW_VISIBLE_MASK)))
    {
        mrp_debug("nothing to do");
        return false;
    }

    id_surface = win->surfaceid;
    visibility = u->visible ? 1 : 0;
    anim_type  = visibility ? MRP_WAYLAND_ANIMATION_SHOW :
                              MRP_WAYLAND_ANIMATION_HIDE;

    if (!(sf = surface_find(wm, id_surface))) {
        mrp_debug("can't find surface %s",
                  surface_id_print(id_surface, buf, sizeof(buf)));
        return false;
    }

    set_window_animation(win, anim_type, anims);

    mrp_debug("calling ivi_controller_surface_set_visibility"
              "(ivi_controller_surface=%p, visibility=%u)",
              sf->ctrl_surface, visibility);

    ivi_controller_surface_set_visibility(sf->ctrl_surface, visibility);

    return true;
}

static bool set_window_active(mrp_wayland_window_t *win,
                              mrp_wayland_window_update_mask_t passthrough,
                              mrp_wayland_window_update_t *u)
{
    static mrp_wayland_active_t focus_mask = MRP_WAYLAND_WINDOW_ACTIVE_POINTER|
                                             MRP_WAYLAND_WINDOW_ACTIVE_TOUCH  ;

    mrp_glm_window_manager_t *wm = (mrp_glm_window_manager_t *)win->wm;
    int32_t id_surface = win->surfaceid;
    ctrl_surface_t *sf;
    int32_t focus_enabled;
    char id_str[256];

    surface_id_print(id_surface, id_str, sizeof(id_str));

    if (u->active == win->active &&
        !(passthrough & MRP_WAYLAND_WINDOW_ACTIVE_MASK))
    {
        mrp_debug("nothing to do");
        return false;
    }

    if (!(sf = surface_find(wm, id_surface))) {
        mrp_debug("can't find surface %s", id_str);
        return false;
    }

    focus_enabled = (u->active & focus_mask) ? 1 : 0;

    mrp_debug("calling ivi_controller_surface_set_input_focus"
              "(struct ivi_controller_surface=%p, enabled=%d)",
              sf->ctrl_surface, focus_enabled);


    ivi_controller_surface_set_input_focus(sf->ctrl_surface,
                                      IVI_CONTROLLER_SURFACE_INPUT_DEVICE_ALL,
                                      focus_enabled);

    return true;
}

static void window_request(mrp_wayland_window_t *win,
                           mrp_wayland_window_update_t *u,
                           mrp_wayland_animation_t *anims,
                           uint32_t framerate)
{
#if 0
    static mrp_wayland_window_update_mask_t area_mask =
        MRP_WAYLAND_WINDOW_AREA_MASK;
#endif
    static mrp_wayland_window_update_mask_t mapped_mask =
        MRP_WAYLAND_WINDOW_MAPPED_MASK;
    static mrp_wayland_window_update_mask_t geometry_mask =
        /* MRP_WAYLAND_WINDOW_NODEID_MASK   | */
        MRP_WAYLAND_WINDOW_POSITION_MASK |
        MRP_WAYLAND_WINDOW_SIZE_MASK     ;
    static mrp_wayland_window_update_mask_t opacity_mask =
        MRP_WAYLAND_WINDOW_OPACITY_MASK;
    static mrp_wayland_window_update_mask_t raise_mask =
        MRP_WAYLAND_WINDOW_RAISE_MASK;
    static mrp_wayland_window_update_mask_t layer_mask =
        MRP_WAYLAND_WINDOW_LAYER_MASK;
    static mrp_wayland_window_update_mask_t visible_mask =
        MRP_WAYLAND_WINDOW_VISIBLE_MASK;
    static mrp_wayland_window_update_mask_t active_mask =
        MRP_WAYLAND_WINDOW_ACTIVE_MASK;


    mrp_wayland_t *wl;
    mrp_glm_window_manager_t *wm;
    mrp_wayland_window_update_mask_t passthrough;
    mrp_wayland_window_update_mask_t mask;
    bool changed;
    char wbuf[2048];
    char abuf[1024];

    MRP_ASSERT(win && win->wm && win->wm->proxy && win->wm->interface &&
               win->wm->interface->wl && u, "invalid argument");

    wm = (mrp_glm_window_manager_t *)win->wm;
    wl = wm->interface->wl;
    passthrough = wm->passthrough.window_request;
    mask = u->mask;
    changed = false;

    mrp_wayland_window_request_print(u, wbuf, sizeof(wbuf));
    mrp_wayland_animation_print(anims, abuf, sizeof(abuf));
    mrp_debug("request for window %d update:%s\n   animations:%s",
              win->surfaceid, wbuf, abuf);

    while (mask) {
        if ((mask & layer_mask)) {
            changed |= set_window_layer(win, passthrough, u);
            mask &= ~layer_mask;
        }
        else if ((mask & mapped_mask))  {
            changed |= set_window_mapped(win, passthrough, u,
                                         anims, framerate);
            mask &= ~(mapped_mask);
        }
#if 0
        else if ((mask & area_mask))  {
            changed |= set_window_area(win, passthrough, u, anims);
            mask &= ~(area_mask | geometry_mask);
        }
#endif
        else if ((mask & geometry_mask)) {
            changed |= set_window_geometry(win, passthrough, u, anims);
            mask &= ~geometry_mask;
        }
        else if ((mask & opacity_mask)) {
            changed |= set_window_opacity(win, passthrough, u);
            mask &= ~opacity_mask;
        }
        else if ((mask & raise_mask)) {
            changed |= raise_window(win, passthrough, u);
            mask &= ~raise_mask;
        }
        else if ((mask & visible_mask)) {
            changed |= set_window_visibility(win, passthrough, u, anims);
            mask &= ~visible_mask;
        }
        else if ((mask & active_mask)) {
            changed |= set_window_active(win, passthrough, u);
            mask &= ~active_mask;
        }
        else {
            mask = 0;
        }
    }

    if (changed) {
        mrp_debug("calling ivi_controller_commit_changes()");
        ivi_controller_commit_changes((struct ivi_controller *)wm->proxy);
    }

    mrp_wayland_flush(wl);
}


static void buffer_request(mrp_wayland_window_manager_t *wm,
                           const char *shmname,
                           uint32_t bufsize,
                           uint32_t bufnum)
{
    (void)bufnum;
    (void)bufsize;

    MRP_ASSERT(wm && wm->proxy && shmname, "invalid argument");

    mrp_log_warning("system-controller: buffer_request is not supported in "
                    "Genivi Layer Management");
}

static uint32_t wid_hash(const void *pkey)
{
    uint32_t key = (uint32_t)(pkey - NULL);

    return key % MRP_WAYLAND_WINDOW_BUCKETS;
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


static int wid_compare(const void *pkey1, const void *pkey2)
{
    return (pkey1 == pkey2)  ? 0 : (pkey1 < pkey2 ? -1 : +1);
}


static int id_compare(const void *pkey1, const void *pkey2)
{
    int32_t key1 = *(int32_t *)pkey1;
    int32_t key2 = *(int32_t *)pkey2;

    return (key1 == key2) ? 0 : ((key1 < key2) ? -1 : 1);
}

static int32_t get_parent_pid(int32_t pid)
{
    int fd;
    char path[256];
    char buf[1024];
    ssize_t size;
    char *ppid_line, *c, *e;
    int32_t ppid;

    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    if ((fd = open(path, O_RDONLY)) < 0)
        return -1;

    while ((size = read(fd, buf, sizeof(buf)-1)) <= 0) {
        if (errno != EINTR) {
            close(fd);
            return -1;
        }
    }

    close(fd);

    buf[size] = 0;

    if ((ppid_line = strstr(buf, "PPid:"))) {
        for (c = ppid_line + 5; (*c == ' ' || *c == '\t');  c++)
            ;

        if ((ppid = strtol(c, &e, 10)) > 0 && e > c && *e == '\n')
            return ppid;
    }

    return -1;
}

static void get_binary_basename(int32_t pid, char *buf, int len)
{
    int fd;
    char path[PATH_MAX];
    char cmdline[PATH_MAX];
    char *bnam;
    ssize_t size;

    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

    buf[0] = 0;

    if ((fd = open(path, O_RDONLY)) < 0)
        return;

    while ((size = read(fd, cmdline, sizeof(cmdline))) <= 0) {
        if (errno != EINTR) {
            close(fd);
            return;
        }
    }

    close(fd);

    /* If we read more than allocated, truncate */
    if (size >= PATH_MAX)
        cmdline[PATH_MAX - 1] = 0;
    else
        cmdline[size] = 0;

    bnam = basename(cmdline);

    strncpy(buf, bnam, len-1);
    buf[len-1] = 0;
}

static void get_appid(int32_t pid, char *buf, int len)
{
    int ppid;

    if (!buf || len < 2)
        return;

    buf[0] = 0;

    if ((ppid = pid) > 0) {
        while (aul_app_get_appid_bypid(ppid, buf, len) != AUL_R_OK) {
            if ((ppid = get_parent_pid(ppid)) <= 1) {
                get_binary_basename(pid, buf, len);
                break;
            }
        }
    }
}
