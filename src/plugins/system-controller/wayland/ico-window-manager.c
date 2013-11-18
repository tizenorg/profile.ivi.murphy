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

#include <ico-uxf-weston-plugin/ico_window_mgr-client-protocol.h>

#include "ico-window-manager.h"
#include "layer.h"
#include "output.h"
#include "animation.h"
#include "window.h"
#include "window-manager.h"
#include "area.h"

#define MAX_COORDINATE       16383

#define SURFACE_TO_NODE(s)   (((s) >> 16) & 0xFF)
#define SURFACE_TO_HOST(s)   (((s) >> 24) & 0xFF)

struct mrp_ico_window_manager_s {
    MRP_WAYLAND_WINDOW_MANAGER_COMMON;
};


static bool window_manager_constructor(mrp_wayland_t *,mrp_wayland_object_t *);

static void window_created_callback(void *, struct ico_window_mgr *, uint32_t,
                                    const char *,int32_t,const char *,int32_t);
static void window_name_callback(void *, struct ico_window_mgr *,
                                 uint32_t, const char *);
static void window_destroyed_callback(void *,struct ico_window_mgr *,uint32_t);
static void window_visible_callback(void *, struct ico_window_mgr *, uint32_t,
                                    int32_t, int32_t, int32_t);
static void window_configure_callback(void *, struct ico_window_mgr *,uint32_t,
                                      uint32_t, int32_t, uint32_t, int32_t,
                                      int32_t, int32_t,int32_t, int32_t);
static void window_active_callback(void *, struct ico_window_mgr *, uint32_t,
                                   int32_t);
static void layer_visible_callback(void *, struct ico_window_mgr *,
                                   uint32_t, int32_t);
static void app_surfaces_callback(void *, struct ico_window_mgr *,
                                  const char *, int32_t, struct wl_array *);
static void map_surface_callback(void *, struct ico_window_mgr *, int32_t,
                                 uint32_t, uint32_t, uint32_t,
                                 int32_t,int32_t, int32_t, uint32_t);


static void layer_request(mrp_wayland_layer_t *,
                          mrp_wayland_layer_update_t *);

static mrp_wayland_window_t *find_window(mrp_ico_window_manager_t *, int32_t,
                                         const char *);
static int32_t set_window_animation(mrp_wayland_window_t *,
                                    mrp_wayland_animation_type_t,
                                    mrp_wayland_animation_t *);
static void set_window_area(mrp_wayland_window_t *,
                            mrp_wayland_window_update_t *,
                            mrp_wayland_animation_t *);
static void set_window_geometry(mrp_wayland_window_t *,
                                mrp_wayland_window_update_t *,
                                mrp_wayland_animation_t *);
static void set_window_alignment(mrp_wayland_window_t *,
                                 mrp_wayland_window_update_t *);
static void set_window_visible(mrp_wayland_window_t *,
                               mrp_wayland_window_update_t *,
                               mrp_wayland_animation_t *);
static void set_window_active(mrp_wayland_window_t *,
                              mrp_wayland_window_update_t *);
static void set_window_mapped(mrp_wayland_window_t *,
                              mrp_wayland_window_update_t *,
                              uint32_t);
static void set_window_layer(mrp_wayland_window_t *,
                             mrp_wayland_window_update_t *);
static void window_request(mrp_wayland_window_t *,
                           mrp_wayland_window_update_t *,
                           mrp_wayland_animation_t *, uint32_t);

static mrp_wayland_layer_type_t get_layer_type(uint32_t);


bool mrp_ico_window_manager_register(mrp_wayland_t *wl)
{
    mrp_wayland_factory_t factory;

    factory.size = sizeof(mrp_ico_window_manager_t);
    factory.interface = &ico_window_mgr_interface;
    factory.constructor = window_manager_constructor;
    factory.destructor = NULL;

    mrp_wayland_register_interface(wl, &factory);

    return true;
}

static bool window_manager_constructor(mrp_wayland_t *wl,
                                       mrp_wayland_object_t *obj)
{
    static struct ico_window_mgr_listener listener =  {
        .window_created   = window_created_callback,
        .window_name      = window_name_callback,
        .window_destroyed = window_destroyed_callback,
        .window_visible   = window_visible_callback,
        .window_configure = window_configure_callback,
        .window_active    = window_active_callback,
        .layer_visible    = layer_visible_callback,
        .app_surfaces     = app_surfaces_callback,
        .map_surface      = map_surface_callback
    };

    mrp_ico_window_manager_t *wm = (mrp_ico_window_manager_t *)obj;
    int sts;

    MRP_ASSERT(wm, "invalid argument");

    wm->layer_request = layer_request;
    wm->window_request = window_request;

    sts = ico_window_mgr_add_listener((struct ico_window_mgr *)wm->proxy,
                                      &listener, wm);

    if (sts < 0)
        return false;

    ico_window_mgr_declare_manager((struct ico_window_mgr *)wm->proxy,
                                   ICO_WINDOW_MGR_DECLARE_MANAGER_MANAGER);

    mrp_wayland_register_window_manager(wl, (mrp_wayland_window_manager_t*)wm);

    return true;
}


static void window_created_callback(void *data,
                                    struct ico_window_mgr *ico_window_mgr,
                                    uint32_t surfaceid,
                                    const char *winname,
                                    int32_t pid,
                                    const char *appid,
                                    int32_t layertype)
{
    mrp_ico_window_manager_t *wm = (mrp_ico_window_manager_t *)data;
    mrp_wayland_t *wl;
    mrp_wayland_window_update_t u;

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)wm->proxy,
               "confused with data structures");

    wl = wm->interface->wl;

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_WINDOW_SURFACEID_MASK |
             MRP_WAYLAND_WINDOW_NAME_MASK      |
             MRP_WAYLAND_WINDOW_APPID_MASK     |
             MRP_WAYLAND_WINDOW_PID_MASK       |
             MRP_WAYLAND_WINDOW_LAYERTYPE_MASK ;

    u.surfaceid = surfaceid;
    u.name = winname ? winname : "";
    u.appid = appid ? appid : "";
    u.pid = pid;
    u.layertype = get_layer_type(layertype);

    mrp_debug("surfaceid=%d, winname='%s' pid=%d appid='%s' layertype='%d'",
              u.surfaceid, u.name, u.pid, u.appid, u.layertype);

    mrp_wayland_window_create(wl, &u);
}

static void window_name_callback(void *data,
                                 struct ico_window_mgr *ico_window_mgr,
                                 uint32_t surfaceid,
                                 const char *winname)
{
    mrp_ico_window_manager_t *wm = (mrp_ico_window_manager_t *)data;
    mrp_wayland_window_t *win;
    mrp_wayland_window_update_t u;

    MRP_ASSERT(wm && wm->interface, "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)wm->proxy,
               "confused with data structures");

    if (!winname) {
        mrp_log_error("Missing window name in %s()", __FUNCTION__);
        return;
    }

    mrp_debug("surfaceid=%d, winname='%s'", surfaceid, winname);

    if (!(win = find_window(wm, surfaceid, "update")))
        return;

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_WINDOW_NAME_MASK;
    u.name = winname;

    mrp_wayland_window_update(win, MRP_WAYLAND_WINDOW_NAMECHANGE, &u);
}

static void window_destroyed_callback(void *data,
                                      struct ico_window_mgr *ico_window_mgr,
                                      uint32_t surfaceid)
{
    mrp_ico_window_manager_t *wm = (mrp_ico_window_manager_t *)data;
    mrp_wayland_window_t *win;

    MRP_ASSERT(wm && wm->interface, "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)wm->proxy,
               "confused with data structures");

    mrp_debug("surfaceid=%d", surfaceid);

    if (!(win = find_window(wm, surfaceid, "destruction")))
        return;

    mrp_wayland_window_destroy(win);
}

static void window_visible_callback(void *data,
                                    struct ico_window_mgr *ico_window_mgr,
                                    uint32_t surfaceid,
                                    int32_t visible,
                                    int32_t raise,
                                    int32_t hint)
{
    mrp_ico_window_manager_t *wm = (mrp_ico_window_manager_t *)data;
    mrp_wayland_window_t *win;
    mrp_wayland_window_update_t u;

    MRP_UNUSED(hint);

    MRP_ASSERT(wm && wm->interface, "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)wm->proxy,
               "confused with data structures");

    mrp_debug("surfaceid=%d visible=%d raise=%d hint=%d",
              surfaceid, visible, raise, hint);

    if (!(win = find_window(wm, surfaceid, "visibility update")))
        return;

    memset(&u, 0, sizeof(u));
    if (visible != ICO_WINDOW_MGR_V_NOCHANGE) {
        u.mask |= MRP_WAYLAND_WINDOW_VISIBLE_MASK;
        u.visible = visible ? true : false;
    }
    if (raise != ICO_WINDOW_MGR_V_NOCHANGE) {
        u.mask |= MRP_WAYLAND_WINDOW_RAISE_MASK;
        u.raise = raise ? true : false;
    }

    if (u.mask)
        mrp_wayland_window_update(win, MRP_WAYLAND_WINDOW_VISIBLE, &u);
    else
        mrp_debug("nothing to do");
}

static void window_configure_callback(void *data,
                                      struct ico_window_mgr *ico_window_mgr,
                                      uint32_t surfaceid,
                                      uint32_t node,
                                      int32_t layertype,
                                      uint32_t layer,
                                      int32_t x,
                                      int32_t y,
                                      int32_t width,
                                      int32_t height,
                                      int32_t hint)
{
    mrp_ico_window_manager_t *wm = (mrp_ico_window_manager_t *)data;
    mrp_wayland_window_t *win;
    mrp_wayland_t *wl;
    mrp_wayland_window_update_t u;

    MRP_UNUSED(layertype);
    MRP_UNUSED(hint);

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)wm->proxy,
               "confused with data structures");

    wl = wm->interface->wl;

    mrp_debug("surfaceid=%d node=%u layertype=%d layer=%u position=%d,%d "
              "size=%dx%d hint=%d", surfaceid, node, layertype, layer,
              x,y, width,height, hint);

    if (!(win = find_window(wm, surfaceid, "configuration update")))
        return;

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_WINDOW_NODEID_MASK    |
             MRP_WAYLAND_WINDOW_LAYERTYPE_MASK ;
    u.nodeid = node;
    u.layertype = layertype;

    if (hint == ICO_WINDOW_MGR_HINT_CHANGE) {
        if (x <= MAX_COORDINATE) {
            u.mask |= MRP_WAYLAND_WINDOW_X_MASK;
            u.x = x;
        }
        if (y <= MAX_COORDINATE) {
            u.mask |= MRP_WAYLAND_WINDOW_Y_MASK;
            u.y = y;
        }
        if (width <= MAX_COORDINATE) {
            u.mask |= MRP_WAYLAND_WINDOW_WIDTH_MASK;
            u.width = width;
        }
        if (height <= MAX_COORDINATE) {
            u.mask |= MRP_WAYLAND_WINDOW_HEIGHT_MASK;
            u.height = height;
        }
    }

    if (!(u.layer = mrp_wayland_layer_find(wl, layer)))
        mrp_log_error("can't find layer %u", layer);
    else
        u.mask |= MRP_WAYLAND_WINDOW_LAYER_MASK;

    mrp_wayland_window_update(win, MRP_WAYLAND_WINDOW_CONFIGURE, &u);
}

static void window_active_callback(void *data,
                                   struct ico_window_mgr *ico_window_mgr,
                                   uint32_t surfaceid,
                                   int32_t active)
{
    mrp_ico_window_manager_t *wm = (mrp_ico_window_manager_t *)data;
    mrp_wayland_window_t *win;
    mrp_wayland_window_update_t u;

    MRP_ASSERT(wm && wm->interface, "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)wm->proxy,
               "confused with data structures");

    mrp_debug("surfaceid=%u active=%d", surfaceid, active);

    if (!(win = find_window(wm, surfaceid, "state update")))
        return;

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_WINDOW_ACTIVE_MASK;
    u.active = 0;

    if ((active & MRP_WAYLAND_WINDOW_ACTIVE_POINTER))
        u.active |= ICO_WINDOW_MGR_ACTIVE_POINTER;
    if ((active & MRP_WAYLAND_WINDOW_ACTIVE_KEYBOARD))
        u.active |= ICO_WINDOW_MGR_ACTIVE_KEYBOARD;
    if ((active & MRP_WAYLAND_WINDOW_ACTIVE_SELECTED))
        u.active |= ICO_WINDOW_MGR_ACTIVE_SELECTED;

    mrp_wayland_window_update(win, MRP_WAYLAND_WINDOW_ACTIVE, &u);
}

static void layer_visible_callback(void *data,
                                   struct ico_window_mgr *ico_window_mgr,
                                   uint32_t layerid,
                                   int32_t visible)
{
    mrp_ico_window_manager_t *wm = (mrp_ico_window_manager_t *)data;
    mrp_wayland_t *wl;
    mrp_wayland_layer_t *layer;
    mrp_wayland_layer_update_t u;

    MRP_ASSERT(wm && wm->interface && wm->interface->wl, "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)wm->proxy,
               "confused with data structures");

    wl = wm->interface->wl;

    mrp_debug("layerid=%u visible=%d", layerid, visible);

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_WINDOW_VISIBLE_MASK;
    u.visible = visible;

    if (!(layer = mrp_wayland_layer_find(wl, layerid))) {
        mrp_log_error("can't find layer %u", layerid);
        return;
    }

    mrp_wayland_layer_update(layer, MRP_WAYLAND_LAYER_VISIBLE, &u);
}

static void app_surfaces_callback(void *data,
                                  struct ico_window_mgr *ico_window_mgr,
                                  const char *appid,
                                  int32_t pid,
                                  struct wl_array *surfaces)
{
    mrp_ico_window_manager_t *wm = (mrp_ico_window_manager_t *)data;

    MRP_UNUSED(pid);
    MRP_UNUSED(surfaces);

    MRP_ASSERT(wm, "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)wm->proxy,
               "confused with data structures");

    mrp_debug("app '%s' surfaces:",
              appid);
}

static void map_surface_callback(void *data,
                                 struct ico_window_mgr *ico_window_mgr,
                                 int32_t event,
                                 uint32_t surfaceid,
                                 uint32_t type,
                                 uint32_t target,
                                 int32_t width,
                                 int32_t height,
                                 int32_t stride,
                                 uint32_t format)
{
    mrp_ico_window_manager_t *wm = (mrp_ico_window_manager_t *)data;

    MRP_UNUSED(event);
    MRP_UNUSED(type);
    MRP_UNUSED(target);
    MRP_UNUSED(width);
    MRP_UNUSED(height);
    MRP_UNUSED(stride);
    MRP_UNUSED(format);

    MRP_ASSERT(wm, "invalid argument");
    MRP_ASSERT(ico_window_mgr == (struct ico_window_mgr *)wm->proxy,
               "confused with data structures");

    mrp_debug("event=%d surfaceid=%u type=%d target=%d size=%dx%d stride=%d "
              "format=%u", event, surfaceid, type, target, width,height,
              stride, format);
}

static void set_layer_visible(mrp_wayland_layer_t *layer,
                              mrp_wayland_layer_update_t *u)
{
    struct ico_window_mgr *ico_window_mgr;
    mrp_wayland_layer_update_mask_t mask;
    int32_t visible;
    bool need_visibility_change;

    ico_window_mgr = (struct ico_window_mgr *)layer->wm->proxy;
    mask = u->mask;

    need_visibility_change = false;
    if (!(mask & MRP_WAYLAND_LAYER_VISIBLE_MASK) ||
        (u->visible && layer->visible) || (!u->visible && !layer->visible))
    {
        visible = ICO_WINDOW_MGR_V_NOCHANGE;
    }
    else {
        visible = u->visible ? ICO_WINDOW_MGR_VISIBLE_SHOW :
                               ICO_WINDOW_MGR_VISIBLE_HIDE;
        need_visibility_change = true;
    }

    if (!need_visibility_change) {
        mrp_debug("nothing to do");
        return;
    }

    mrp_debug("calling ico_window_mgr_set_layer_visible(layer=%d, visible=%d)",
              layer->layerid, visible);

    ico_window_mgr_set_layer_visible(ico_window_mgr, layer->layerid, visible);
}

static void layer_request(mrp_wayland_layer_t *layer,
                          mrp_wayland_layer_update_t *u)
{
    mrp_wayland_t *wl;
    mrp_wayland_layer_update_mask_t mask;
    char buf[2048];

    MRP_ASSERT(layer && layer->wm && layer->wm->proxy &&
               layer->wm->interface && layer->wm->interface->wl,
               "invalid argument");

    wl = layer->wm->interface->wl;
    mask = u->mask;

    mrp_wayland_layer_request_print(u, buf, sizeof(buf));
    mrp_debug("request for layer %d update:%s", layer->layerid, buf);

    while (mask) {
        if ((mask & MRP_WAYLAND_LAYER_VISIBLE_MASK)) {
            set_layer_visible(layer, u);
            mask &= ~MRP_WAYLAND_LAYER_VISIBLE_MASK;
        }
        else {
            mask = 0;
        }
    }

    mrp_wayland_flush(wl);
}


static mrp_wayland_window_t *find_window(mrp_ico_window_manager_t *wm,
                                         int32_t surfaceid,
                                         const char *operation)
{
    mrp_wayland_t *wl;
    mrp_wayland_window_t *win = NULL;

    if (!(wm->interface) || !(wl = wm->interface->wl) ||
        !(win = mrp_wayland_window_find(wl, surfaceid)))
    {
        mrp_log_error("window %d not found. No %s", surfaceid, operation);
    }

    return win;
}

static int32_t set_window_animation(mrp_wayland_window_t *win,
                                    mrp_wayland_animation_type_t type,
                                    mrp_wayland_animation_t *anims)
{
    static int32_t ico_types[MRP_WAYLAND_ANIMATION_MAX] = {
        [MRP_WAYLAND_ANIMATION_HIDE]   = ICO_WINDOW_MGR_ANIMATION_TYPE_HIDE,
        [MRP_WAYLAND_ANIMATION_SHOW]   = ICO_WINDOW_MGR_ANIMATION_TYPE_SHOW,
        [MRP_WAYLAND_ANIMATION_MOVE]   = ICO_WINDOW_MGR_ANIMATION_TYPE_MOVE,
        [MRP_WAYLAND_ANIMATION_RESIZE] = ICO_WINDOW_MGR_ANIMATION_TYPE_RESIZE
    };

    struct ico_window_mgr *ico_window_mgr;
    mrp_wayland_animation_t *a;
    int32_t flags;

    ico_window_mgr = (struct ico_window_mgr *)win->wm->proxy;
    flags = ICO_WINDOW_MGR_FLAGS_NO_CONFIGURE;

    if (anims && type >= 0 && type < MRP_WAYLAND_ANIMATION_MAX) {
        a = anims + type;

        if (a->name && a->name[0] && a->time > 0) {
            mrp_debug("calling ico_window_mgr_set_animation"
                      "(surfaceid=%d type=%d, animation='%s' time=%d)",
                      win->surfaceid, ico_types[type], a->name, a->time);

            ico_window_mgr_set_animation(ico_window_mgr, win->surfaceid,
                                         ico_types[type], a->name, a->time);
            flags = ICO_WINDOW_MGR_FLAGS_ANIMATION;
        }
    }

    return flags;
}

static void set_window_area(mrp_wayland_window_t *win,
                            mrp_wayland_window_update_t *u,
                            mrp_wayland_animation_t *anims)
{
    mrp_wayland_area_t *area;
    mrp_wayland_output_t *output;

    if (!(area = u->area)) {
        mrp_log_error("system-controller: request for area change but "
                      "no area defined");
        return;
    }

    output = area->output;

    MRP_ASSERT(output, "confused withdata structures");

    if (win->nodeid != output->outputid) {
        u->mask |= MRP_WAYLAND_WINDOW_NODEID_MASK;
        u->nodeid = output->outputid;
    }

    u->mask |= MRP_WAYLAND_WINDOW_POSITION_MASK | MRP_WAYLAND_WINDOW_SIZE_MASK;

    u->x = area->x;
    u->y = area->y;

    u->width  = area->width;
    u->height = area->height;

    set_window_geometry(win, u, anims);
}


static void set_window_geometry(mrp_wayland_window_t *win,
                                mrp_wayland_window_update_t *u,
                                mrp_wayland_animation_t *anims)
{
    struct ico_window_mgr *ico_window_mgr;
    mrp_wayland_window_update_mask_t mask;
    int32_t node;
    int32_t x, y;
    int32_t width, height;
    int32_t flags;
    bool first_time;
    bool need_nodechanging;
    bool need_positioning;
    bool need_resizing;

    ico_window_mgr = (struct ico_window_mgr *)win->wm->proxy;
    mask = u->mask;
    flags = ICO_WINDOW_MGR_FLAGS_NO_CONFIGURE;
    first_time = win->nodeid < 0;

    node = (mask & MRP_WAYLAND_WINDOW_NODEID_MASK) ? u->nodeid : win->nodeid;
    if (node < 0)
        node = SURFACE_TO_NODE(win->surfaceid);
    need_nodechanging = (mask & MRP_WAYLAND_WINDOW_NODEID_MASK) ||
                        node != win->nodeid;

    need_positioning = false;
    if (!(mask & MRP_WAYLAND_WINDOW_X_MASK) || (u->x == win->x)) {
        if (!first_time)
            x = ICO_WINDOW_MGR_V_NOCHANGE;
        else {
            x = win->x;
            need_positioning = true;
        }
    }
    else {
        x = u->x;
        need_positioning = true;
    }
    if (!(mask & MRP_WAYLAND_WINDOW_Y_MASK) || (u->y == win->y)) {
        if (!first_time)
            y = ICO_WINDOW_MGR_V_NOCHANGE;
        else {
            y = win->y;
            need_positioning = true;
        }
    }
    else {
        y = u->y;
        need_positioning = true;
    }

    need_resizing = false;
    if (!(mask & MRP_WAYLAND_WINDOW_WIDTH_MASK) || (u->width == win->width)) {
        if (!first_time)
            width = ICO_WINDOW_MGR_V_NOCHANGE;
        else {
            width = win->width;
            need_positioning = true;
        }
    }
    else {
        width = u->width;
        need_resizing = true;
    }
    if (!(mask&MRP_WAYLAND_WINDOW_HEIGHT_MASK) || (u->height == win->height)) {
        if (!first_time)
            height = ICO_WINDOW_MGR_V_NOCHANGE;
        else {
            height = win->height;
            need_positioning = true;
        }
    }
    else {
        height = u->height;
        need_resizing = true;
    }

    if (!need_nodechanging && !need_positioning && !need_resizing) {
        mrp_debug("nothing to do");
        return;
    }

    flags = ICO_WINDOW_MGR_FLAGS_NO_CONFIGURE;
    if (need_positioning)
        flags |= set_window_animation(win, MRP_WAYLAND_ANIMATION_MOVE, anims);
    if (need_resizing)
        flags |= set_window_animation(win, MRP_WAYLAND_ANIMATION_RESIZE,anims);
    if ((flags & ~ICO_WINDOW_MGR_FLAGS_NO_CONFIGURE))
        flags &= ~ICO_WINDOW_MGR_FLAGS_NO_CONFIGURE;

    mrp_debug("calling ico_window_mgr_set_positionsize"
              "(surfaceid=%d, node=%u position=%d,%d size=%dx%d flags=%d)",
              win->surfaceid, node, x,y, width,height, flags);

    ico_window_mgr_set_positionsize(ico_window_mgr, win->surfaceid, node,
                                    x,y, width,height, flags);
}

static void set_window_alignment(mrp_wayland_window_t *win,
                                 mrp_wayland_window_update_t *u)
{
    struct ico_window_mgr *ico_window_mgr;
    mrp_wayland_area_t *area;
    uint32_t attrs;

    ico_window_mgr = (struct ico_window_mgr *)win->wm->proxy;

    if (!(area = u->area)) {
        mrp_log_error("attempt to set NULL area");
        return;
    }

    if (win->area) {
        if (win->area->keepratio == area->keepratio &&
            win->area->align == area->align)
        {
            mrp_debug("nothing to do");
            return;
        }
    }

    win->area = area;

    if (!area->keepratio)
        attrs = 0;
    else {
        attrs = ICO_WINDOW_MGR_ATTR_FIXED_ASPECT;

        switch ((area->align & MRP_WAYLAND_AREA_ALIGN_HMASK)) {
        case MRP_WAYLAND_AREA_ALIGN_LEFT:
            attrs |= ICO_WINDOW_MGR_ATTR_ALIGN_LEFT;
            break;
        case MRP_WAYLAND_AREA_ALIGN_RIGHT:
            attrs |= ICO_WINDOW_MGR_ATTR_ALIGN_RIGHT;
            break;
        default:
            break;
        }

        switch ((area->align & MRP_WAYLAND_AREA_ALIGN_VMASK)) {
        case MRP_WAYLAND_AREA_ALIGN_TOP:
            attrs |= ICO_WINDOW_MGR_ATTR_ALIGN_TOP;
            break;
        case MRP_WAYLAND_AREA_ALIGN_BOTTOM:
            attrs |= ICO_WINDOW_MGR_ATTR_ALIGN_BOTTOM;
            break;
        default:
            break;
        }
    }

    mrp_debug("calling ico_window_mgr_set_attributes(surfaceid=%d attrs=0x%x)",
              win->surfaceid, attrs);

    ico_window_mgr_set_attributes(ico_window_mgr, win->surfaceid, attrs);

    return;
}

static void set_window_visible(mrp_wayland_window_t *win,
                               mrp_wayland_window_update_t *u,
                               mrp_wayland_animation_t *anims)
{
    struct ico_window_mgr *ico_window_mgr;
    mrp_wayland_window_update_mask_t mask;
    mrp_wayland_animation_type_t anim_type;
    int32_t visible;
    int32_t raise;
    int32_t flags;
    bool need_visibility_change;
    bool need_raising;

    ico_window_mgr = (struct ico_window_mgr *)win->wm->proxy;
    mask = u->mask;

    need_visibility_change = false;
    if (!(mask & MRP_WAYLAND_WINDOW_VISIBLE_MASK) ||
        (u->visible && win->visible) || (!u->visible && !win->visible))
    {
        visible = ICO_WINDOW_MGR_V_NOCHANGE;
        anim_type = 0;
    }
    else {
        visible = u->visible ? ICO_WINDOW_MGR_VISIBLE_SHOW :
                               ICO_WINDOW_MGR_VISIBLE_HIDE;
        anim_type = visible ? MRP_WAYLAND_ANIMATION_SHOW :
                              MRP_WAYLAND_ANIMATION_HIDE;
        win->visible = u->visible;
        need_visibility_change = true;
    }

    need_raising = false;
    if (!(mask & MRP_WAYLAND_WINDOW_RAISE_MASK) ||
        (u->raise && win->raise) || (!u->raise && !win->raise))
    {
        raise = ICO_WINDOW_MGR_V_NOCHANGE;
    }
    else {
        raise = u->raise ? ICO_WINDOW_MGR_RAISE_RAISE :
                           ICO_WINDOW_MGR_RAISE_LOWER;
        win->raise = u->raise;
        need_raising = true;
    }

    if (!need_visibility_change && !need_raising) {
        mrp_debug("nothing to do");
        return;
    }

    if (need_visibility_change)
        flags = set_window_animation(win, anim_type, anims);
    else
        flags = ICO_WINDOW_MGR_FLAGS_NO_CONFIGURE;

    mrp_debug("calling ico_window_mgr_set_visible"
              "(surfaceid=%d, visible=%d, raise=%d, flags=%d)",
              win->surfaceid, visible, raise, flags);

    ico_window_mgr_set_visible(ico_window_mgr, win->surfaceid,
                               visible, raise, flags);
}

static void set_window_active(mrp_wayland_window_t *win,
                              mrp_wayland_window_update_t *u)
{
    struct ico_window_mgr *ico_window_mgr;
    int32_t active;

    ico_window_mgr = (struct ico_window_mgr *)win->wm->proxy;
    active = 0;

    if (u->active == win->active)
        mrp_debug("nothing to do");
    else {
        if ((u->active & MRP_WAYLAND_WINDOW_ACTIVE_POINTER))
            active |= ICO_WINDOW_MGR_ACTIVE_POINTER;
        if ((u->active & MRP_WAYLAND_WINDOW_ACTIVE_KEYBOARD))
            active |= ICO_WINDOW_MGR_ACTIVE_KEYBOARD;
        if ((u->active & MRP_WAYLAND_WINDOW_ACTIVE_SELECTED))
            active |= ICO_WINDOW_MGR_ACTIVE_SELECTED;

        mrp_debug("calling ico_window_mgr_set_active"
                  "(surfaceid=%d, active=0x%x)",
                  win->surfaceid, active);

        ico_window_mgr_set_active(ico_window_mgr, win->surfaceid, active);
    }
}

static void set_window_mapped(mrp_wayland_window_t *win,
                              mrp_wayland_window_update_t *u,
                              uint32_t framerate)
{
    struct ico_window_mgr *ico_window_mgr;

    ico_window_mgr = (struct ico_window_mgr *)win->wm->proxy;

    if ((u->mapped && win->mapped) || (!u->mapped && !win->mapped))
        mrp_debug("nothing to do");
    else {
        if (u->mapped) {
            mrp_debug("calling ico_window_mgr_map_surface"
                      "(surfaceid=%d, framerate=%u)",
                      win->surfaceid, framerate);

            ico_window_mgr_map_surface(ico_window_mgr, win->surfaceid,
                                       framerate);
        }
        else {
            mrp_debug("calling ico_window_mgr_unmap_surface(surfaceid=%d)",
                      win->surfaceid);

            ico_window_mgr_unmap_surface(ico_window_mgr, win->surfaceid);
        }
    }
}

static void set_window_layer(mrp_wayland_window_t *win,
                             mrp_wayland_window_update_t *u)
{
    struct ico_window_mgr *ico_window_mgr;
    uint32_t layer;

    ico_window_mgr = (struct ico_window_mgr *)win->wm->proxy;

    if (win->layer && (u->layer == win->layer)) {
        mrp_debug("nothing to do");
    }
    else {
        layer = u->layer->layerid;

        mrp_debug("calling ico_window_mgr_set_window_layer"
                  "(surfaceid=%d, layer=%u)", win->surfaceid, layer);

        ico_window_mgr_set_window_layer(ico_window_mgr, win->surfaceid, layer);
    }
}

static void window_request(mrp_wayland_window_t *win,
                           mrp_wayland_window_update_t *u,
                           mrp_wayland_animation_t *anims,
                           uint32_t framerate)
{
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
    static mrp_wayland_window_update_mask_t align_mask =
        MRP_WAYLAND_WINDOW_AREA_MASK;

    mrp_wayland_t *wl;
    mrp_wayland_window_update_mask_t mask;
    char wbuf[2048];
    char abuf[1024];

    MRP_ASSERT(win && win->wm && win->wm->proxy && win->wm->interface &&
               win->wm->interface->wl, "invalid argument");

    wl = win->wm->interface->wl;
    mask = u->mask;

    mrp_wayland_window_request_print(u, wbuf, sizeof(wbuf));
    mrp_wayland_animation_print(anims, abuf, sizeof(abuf));
    mrp_debug("request for window %d update:%s\n   animations:%s",
              win->surfaceid, wbuf, abuf);

    while (mask) {
        if ((mask & layer_mask)) {
            set_window_layer(win, u);
            mask &= ~layer_mask;
        }
        else if ((mask & mapped_mask)) {
            set_window_mapped(win, u, framerate);
            mask &= ~mapped_mask;
        }
        else if ((mask & area_mask))  {
            set_window_area(win, u, anims);
            mask &= ~(area_mask | geometry_mask);
        }
        else if ((mask & geometry_mask)) {
            set_window_geometry(win, u, anims);
            mask &= ~geometry_mask;
        }
        else if ((mask & align_mask)) {
            set_window_alignment(win, u);
            mask &= ~align_mask;
        }
        else if ((mask & visible_mask)) {
            set_window_visible(win, u, anims);
            mask &= ~visible_mask;
        }
        else if ((mask & active_mask)) {
            set_window_active(win, u);
            mask &= ~active_mask;
        }
        else {
            mask = 0;
        }
    }

    mrp_wayland_flush(wl);
}

static mrp_wayland_layer_type_t get_layer_type(uint32_t layertype)
{
    switch (layertype) {

    case ICO_WINDOW_MGR_LAYERTYPE_BACKGROUND:
        return MRP_WAYLAND_LAYER_BACKGROUND;

    case ICO_WINDOW_MGR_LAYERTYPE_NORMAL:
        return MRP_WAYLAND_LAYER_APPLICATION;

    case ICO_WINDOW_MGR_LAYERTYPE_FULLSCREEN:
    case ICO_WINDOW_MGR_LAYERTYPE_INPUTPANEL:
        return MRP_WAYLAND_LAYER_INPUT;

    case ICO_WINDOW_MGR_LAYERTYPE_TOUCH:
        return MRP_WAYLAND_LAYER_TOUCH;

    case ICO_WINDOW_MGR_LAYERTYPE_CURSOR:
        return MRP_WAYLAND_LAYER_CURSOR;

    case ICO_WINDOW_MGR_LAYERTYPE_STARTUP:
        return MRP_WAYLAND_LAYER_STARTUP;

    default:
        return MRP_WAYLAND_LAYER_TYPE_UNKNOWN;
    }
}
