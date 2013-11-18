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

#include "window.h"
#include "layer.h"
#include "animation.h"
#include "window-manager.h"

static mrp_wayland_window_update_mask_t update(mrp_wayland_window_t *,
                                               mrp_wayland_window_update_t *);

static char *active_str(mrp_wayland_active_t, char *, size_t);


mrp_wayland_window_t *
mrp_wayland_window_create(mrp_wayland_t *wl, mrp_wayland_window_update_t *u)
{
    mrp_wayland_window_t *win;
    mrp_wayland_window_update_mask_t mask;
    char buf[2048];

    MRP_ASSERT(wl && u && (u->mask & MRP_WAYLAND_WINDOW_SURFACEID_MASK),
               "invalid argument");

    if (!wl->wm) {
        mrp_log_error("failed to create window %d: no window manager",
                      u->surfaceid);
        return NULL;
    }

    if (!(win = mrp_allocz(sizeof(mrp_wayland_window_t)))) {
        mrp_log_error("failed to create window %d: out of memory",
                      u->surfaceid);
        return NULL;
    }

    win->wm = wl->wm;
    win->surfaceid = u->surfaceid;
    win->name = mrp_strdup("");
    win->appid = mrp_strdup("");
    win->nodeid = -1;
    win->x = win->y = -1;
    win->width = win->height = -1;

    if (!mrp_htbl_insert(wl->windows, &win->surfaceid, win)) {
        mrp_log_error("failed to create window: already exists");
        mrp_free(win);
        return NULL;
    }

    mask = update(win, u);

    mrp_wayland_window_print(win, mask, buf,sizeof(buf));
    mrp_debug("window %d created%s", win->surfaceid, buf);

    return win;
}

void mrp_wayland_window_destroy(mrp_wayland_window_t *win)
{
    mrp_wayland_window_manager_t *wm;
    mrp_wayland_interface_t *interface;
    mrp_wayland_t *wl;
    char buf[1024];

    MRP_ASSERT(win && win->wm && win->wm->interface &&
               win->wm->interface->wl, "invalid argument");

    wm = win->wm;
    interface = wm->interface;
    wl = interface->wl;

    mrp_wayland_window_print(win, MRP_WAYLAND_WINDOW_APPID_MASK |
                             MRP_WAYLAND_WINDOW_NAME_MASK,
                             buf, sizeof(buf));
    mrp_debug("destroying window %d%s", win->surfaceid, buf);

    mrp_free(win->name);
    mrp_free(win->appid);

    if ((void *)win != mrp_htbl_remove(wl->windows, &win->surfaceid, false)) {
        mrp_log_error("failed to destroy window %d: confused with "
                      "data structures", win->surfaceid);
        return;
    }

    free(win);
}

mrp_wayland_window_t *mrp_wayland_window_find(mrp_wayland_t *wl,
                                              int32_t surfaceid)
{
    MRP_ASSERT(wl, "invalid argument");

    return (mrp_wayland_window_t *)mrp_htbl_lookup(wl->windows, &surfaceid);
}

void mrp_wayland_window_visibility_request(mrp_wayland_window_t *win,
                                           int32_t visible,
                                           int32_t raise,
                                           const char *animation,
                                           int32_t time)
{
    mrp_wayland_window_manager_t *wm;
    mrp_wayland_window_update_t u;
    mrp_wayland_animation_t *anims;
    mrp_wayland_animation_type_t at;

    MRP_ASSERT(win && win->wm, "invalid argument");

    wm = win->wm;

    memset(&u, 0, sizeof(u));

    if (visible >= 0) {
        u.mask |= MRP_WAYLAND_WINDOW_VISIBLE_MASK;
        u.visible = visible ? true : false;
    }

    if (raise >= 0) {
        u.mask |= MRP_WAYLAND_WINDOW_RAISE_MASK;
        u.raise = raise ? true : false;
    }

    if ((anims = mrp_wayland_animation_create())) {
        at = visible ? MRP_WAYLAND_ANIMATION_SHOW : MRP_WAYLAND_ANIMATION_HIDE;
        mrp_wayland_animation_set(anims, at, animation, time);
    }

    wm->window_request(win, &u, anims, 0);

    mrp_wayland_animation_destroy(anims);
}

void mrp_wayland_window_active_request(mrp_wayland_window_t *win,
                                       mrp_wayland_active_t active)
{
    mrp_wayland_window_manager_t *wm;
    mrp_wayland_window_update_t u;

    MRP_ASSERT(win && win->wm, "invalid argument");

    wm = win->wm;

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_WINDOW_ACTIVE_MASK;
    u.active = active;

    wm->window_request(win, &u, NULL, 0);
}

void mrp_wayland_window_map_request(mrp_wayland_window_t *win,
                                    bool map,
                                    uint32_t framerate)
{
    mrp_wayland_window_manager_t *wm;
    mrp_wayland_window_update_t u;

    MRP_ASSERT(win && win->wm && framerate <= MRP_WAYLAND_FRAMERATE_MAX,
               "invalid argument");

    wm = win->wm;

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_WINDOW_MAPPED_MASK;
    u.mapped = map;

    wm->window_request(win, &u, NULL, framerate);
}

void mrp_wayland_window_geometry_request(mrp_wayland_window_t *win,
                                         int32_t nodeid,
                                         int32_t x,
                                         int32_t y,
                                         int32_t width,
                                         int32_t height,
                                         const char *move_animation,
                                         int32_t move_time,
                                         const char *resize_animation,
                                         int32_t resize_time)
{
    mrp_wayland_window_manager_t *wm;
    mrp_wayland_window_update_t u;
    mrp_wayland_animation_t *anims;
    mrp_wayland_animation_type_t at;
    bool have_move_animation;
    bool have_resize_animation;

    MRP_ASSERT(win && win->wm, "invalid argument");

    wm = win->wm;

    memset(&u, 0, sizeof(u));

    if (nodeid >= 0) {
        u.mask |= MRP_WAYLAND_WINDOW_NODEID_MASK;
        u.nodeid = nodeid;
    }
    else if (win->nodeid >= 0) {
        u.mask |= MRP_WAYLAND_WINDOW_NODEID_MASK;
        u.nodeid = win->nodeid;
    }

    if (x != MRP_WAYLAND_NO_UPDATE) {
        u.mask |= MRP_WAYLAND_WINDOW_X_MASK;
        u.x = x;
    }
    if (y != MRP_WAYLAND_NO_UPDATE) {
        u.mask |= MRP_WAYLAND_WINDOW_Y_MASK;
        u.y = y;
    }

    if (width > 0) {
        u.mask |= MRP_WAYLAND_WINDOW_WIDTH_MASK;
        u.width = width;
    }
    if (height > 0) {
        u.mask |= MRP_WAYLAND_WINDOW_HEIGHT_MASK;
        u.height = height;
    }

    have_move_animation = (move_animation && move_animation[0] &&
                           move_time > 0);
    have_resize_animation = (resize_animation && resize_animation[0] &&
                             resize_time > 0);

    if (!have_move_animation && !have_resize_animation)
        anims = NULL;
    else {
        if ((anims = mrp_wayland_animation_create())) {
            if (have_move_animation) {
                mrp_wayland_animation_set(anims, MRP_WAYLAND_ANIMATION_MOVE,
                                          move_animation, move_time);
            }
            if (have_resize_animation) {
                mrp_wayland_animation_set(anims, MRP_WAYLAND_ANIMATION_RESIZE,
                                          resize_animation, resize_time);
            }
        }
    }

    wm->window_request(win, &u, NULL, 0);

    mrp_wayland_animation_destroy(anims);
}


void mrp_wayland_window_layer_request(mrp_wayland_window_t *win,
                                      int32_t layerid)
{
    mrp_wayland_window_manager_t *wm;
    mrp_wayland_t *wl;
    mrp_wayland_layer_t *layer;
    mrp_wayland_window_update_t u;

    MRP_ASSERT(win && win->wm && win->wm->interface && win->wm->interface->wl,
               "invalid argument");

    wm = win->wm;
    wl = wm->interface->wl;

    if (!(layer = mrp_wayland_layer_find(wl, layerid))) {
        /* this is temporary */
        {
            mrp_wayland_layer_update_t lu;

            memset(&lu, 0, sizeof(lu));
            lu.mask = MRP_WAYLAND_LAYER_LAYERID_MASK;
            lu.layerid = layerid;

            if (!(layer = mrp_wayland_layer_create(wl, &lu))) {
                mrp_log_error("can't find/create layer %d", layerid);
                return;
            }
        }
        /* end of temporary */
    }

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_WINDOW_LAYER_MASK;
    u.layer = layer;

    wm->window_request(win, &u, NULL, 0);
}

void mrp_wayland_window_request(mrp_wayland_t *wl,
                                mrp_wayland_window_update_t *u,
                                mrp_wayland_animation_t *anims,
                                uint32_t framerate)
{
    mrp_wayland_window_t *win;
    mrp_wayland_window_manager_t *wm;

    MRP_ASSERT(wl && u, "invalid arguments");

    if (!(u->mask & MRP_WAYLAND_WINDOW_SURFACEID_MASK) ||
        !(win = mrp_wayland_window_find(wl, u->surfaceid)))
    {
        mrp_debug("can't find window %u: request rejected", u->surfaceid);
    }

    MRP_ASSERT(win->wm, "confused with data structures");

    wm = win->wm;

    wm->window_request(win, u, anims, framerate);
}

void mrp_wayland_window_update(mrp_wayland_window_t *win,
                               mrp_wayland_window_update_t *u)
{
    mrp_wayland_window_manager_t *wm;
    mrp_wayland_interface_t *interface;
    mrp_wayland_t *wl;
    int32_t surfaceid;
    mrp_wayland_window_update_mask_t mask;
    char buf[2048];

    MRP_ASSERT(win && win->wm && win->wm->interface &&
               win->wm->interface->wl && u, "invalid argument");

    wm = win->wm;
    interface = wm->interface;
    wl = interface->wl;

    surfaceid = win->surfaceid;

    if ((u->mask & MRP_WAYLAND_WINDOW_SURFACEID_MASK)) {
        if (u->surfaceid != surfaceid) {
            mrp_log_error("attempt to change surfaceid to %d of "
                          "existing window %d", u->surfaceid, surfaceid);
            return;
        }
    }

    mask = update(win, u);

    if (!mask)
        mrp_debug("window %d update requested but nothing changed", surfaceid);
    else {
        mrp_wayland_window_print(win, mask, buf,sizeof(buf));
        mrp_debug("window %d updated%s", surfaceid, buf);

        if (wl->window_update_callback)
            wl->window_update_callback(wl, mask, win);
    }
}

size_t mrp_wayland_window_print(mrp_wayland_window_t *win,
                                mrp_wayland_window_update_mask_t mask,
                                char *buf,
                                size_t len)
{
#define PRINT(fmt, args...) \
    if (p < e) { p += snprintf(p, e-p, "\n      " fmt , ## args); }

    char *p, *e;
    char as[256];

    e = (p = buf) + len;

    *p = 0;

    if ((mask & MRP_WAYLAND_WINDOW_NAME_MASK))
        PRINT("name: '%s'", win->name);
    if ((mask & MRP_WAYLAND_WINDOW_APPID_MASK))
        PRINT("appid: '%s'", win->appid);
    if ((mask & MRP_WAYLAND_WINDOW_PID_MASK))
        PRINT("pid: %u", win->pid);
    if ((mask & MRP_WAYLAND_WINDOW_NODEID_MASK))
        PRINT("nodeid: %d", win->nodeid);
    if ((mask & MRP_WAYLAND_WINDOW_LAYER_MASK))
        PRINT("layer: '%s'", win->layer ? win->layer->name : "<not set>");
    if ((mask & MRP_WAYLAND_WINDOW_POSITION_MASK))
        PRINT("position: %d,%d", win->x, win->y);
    if ((mask & MRP_WAYLAND_WINDOW_SIZE_MASK))
        PRINT("size: %dx%d", win->width, win->height);
    if ((mask & MRP_WAYLAND_WINDOW_VISIBLE_MASK))
        PRINT("visible: %s", win->visible ? "yes" : "no");
    if ((mask & MRP_WAYLAND_WINDOW_RAISE_MASK))
        PRINT("raise: %s", win->raise ? "yes" : "no");
    if ((mask & MRP_WAYLAND_WINDOW_MAPPED_MASK))
        PRINT("mapped: %s", win->mapped ? "yes" : "no");
    if ((mask & MRP_WAYLAND_WINDOW_ACTIVE_MASK))
        PRINT("active: 0x%x =%s", win->active, active_str(win->active,
                                                          as, sizeof(as)));
    return p - buf;

#undef PRINT
}

size_t mrp_wayland_window_request_print(mrp_wayland_window_update_t *u,
                                        mrp_wayland_window_update_mask_t mask,
                                        char *buf,
                                        size_t len)
{
#define PRINT(fmt, args...) \
    if (p < e) { p += snprintf(p, e-p, "\n      " fmt , ## args); }

    char *p, *e;
    char as[256];

    e = (p = buf) + len;

    *p = 0;

    if ((mask & MRP_WAYLAND_WINDOW_NAME_MASK))
        PRINT("name: '%s'", u->name);
    if ((mask & MRP_WAYLAND_WINDOW_APPID_MASK))
        PRINT("appid: '%s'", u->appid);
    if ((mask & MRP_WAYLAND_WINDOW_PID_MASK))
        PRINT("pid: %u", u->pid);
    if ((mask & MRP_WAYLAND_WINDOW_NODEID_MASK))
        PRINT("nodeid: %d", u->nodeid);
    if ((mask & MRP_WAYLAND_WINDOW_LAYER_MASK))
        PRINT("layer: '%s'", u->layer ? u->layer->name : "<not set>");
    if ((mask & MRP_WAYLAND_WINDOW_POSITION_MASK))
        PRINT("position: %d,%d", u->x, u->y);
    if ((mask & MRP_WAYLAND_WINDOW_SIZE_MASK))
        PRINT("size: %dx%d", u->width, u->height);
    if ((mask & MRP_WAYLAND_WINDOW_VISIBLE_MASK))
        PRINT("visible: %s", u->visible ? "yes" : "no");
    if ((mask & MRP_WAYLAND_WINDOW_RAISE_MASK))
        PRINT("raise: %s", u->raise ? "yes" : "no");
    if ((mask & MRP_WAYLAND_WINDOW_MAPPED_MASK))
        PRINT("mapped: %s", u->mapped ? "yes" : "no");
    if ((mask & MRP_WAYLAND_WINDOW_ACTIVE_MASK))
        PRINT("active: 0x%x =%s", u->active, active_str(u->active,
                                                        as, sizeof(as)));
   return p - buf;

#undef PRINT
}

const char *
mrp_wayland_window_update_mask_str(mrp_wayland_window_update_mask_t mask)
{
    switch (mask) {
    case MRP_WAYLAND_WINDOW_SURFACEID_MASK: return "surfaceid";
    case MRP_WAYLAND_WINDOW_NAME_MASK:      return "name";
    case MRP_WAYLAND_WINDOW_APPID_MASK:     return "appid";
    case MRP_WAYLAND_WINDOW_PID_MASK:       return "pid";
    case MRP_WAYLAND_WINDOW_NODEID_MASK:    return "nodeid";
    case MRP_WAYLAND_WINDOW_LAYER_MASK:     return "layer";
    case MRP_WAYLAND_WINDOW_X_MASK:         return "x";
    case MRP_WAYLAND_WINDOW_Y_MASK:         return "y";
    case MRP_WAYLAND_WINDOW_POSITION_MASK:  return "position";
    case MRP_WAYLAND_WINDOW_WIDTH_MASK:     return "width";
    case MRP_WAYLAND_WINDOW_HEIGHT_MASK:    return "height";
    case MRP_WAYLAND_WINDOW_SIZE_MASK:      return "size";
    case MRP_WAYLAND_WINDOW_VISIBLE_MASK:   return "visible";
    case MRP_WAYLAND_WINDOW_RAISE_MASK:     return "raise";
    case MRP_WAYLAND_WINDOW_ACTIVE_MASK:    return "active";
    case MRP_WAYLAND_WINDOW_MAPPED_MASK:    return "mapped";
    default:                                return "<unknown>";
    }
}

static mrp_wayland_window_update_mask_t update(mrp_wayland_window_t *win,
                                               mrp_wayland_window_update_t *u)
{
    mrp_wayland_window_update_mask_t mask = 0;

    if ((u->mask & MRP_WAYLAND_WINDOW_NAME_MASK)) {
        if (!win->name || strcmp(u->name, win->name)) {
            mask |= MRP_WAYLAND_WINDOW_NAME_MASK;
            mrp_free(win->name);
            win->name = mrp_strdup(u->name);
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_APPID_MASK)) {
        if (!win->appid || strcmp(u->appid, win->appid)) {
            mask |= MRP_WAYLAND_WINDOW_APPID_MASK;
            mrp_free(win->appid);
            win->appid = mrp_strdup(u->appid);
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_PID_MASK)) {
        if (u->pid != win->pid) {
            mask |= MRP_WAYLAND_WINDOW_PID_MASK;
            win->pid = u->pid;
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_NODEID_MASK)) {
        if (u->nodeid != win->nodeid) {
            mask |= MRP_WAYLAND_WINDOW_NODEID_MASK;
            win->nodeid = u->nodeid;
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_LAYER_MASK)) {
        if (u->layer != win->layer) {
            mask |= MRP_WAYLAND_WINDOW_LAYER_MASK;
            win->layer = u->layer;
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_X_MASK)) {
        if (u->x != win->x) {
            mask |= MRP_WAYLAND_WINDOW_X_MASK;
            win->x = u->x;
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_Y_MASK)) {
        if (u->y != win->y) {
            mask |= MRP_WAYLAND_WINDOW_Y_MASK;
            win->y = u->y;
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_WIDTH_MASK)) {
        if (u->width != win->width) {
            mask |= MRP_WAYLAND_WINDOW_WIDTH_MASK;
            win->width = u->width;
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_HEIGHT_MASK)) {
        if (u->height != win->height) {
            mask |= MRP_WAYLAND_WINDOW_HEIGHT_MASK;
            win->height = u->height;
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_VISIBLE_MASK)) {
        if ((u->visible && !win->visible) || (!u->visible && win->visible)) {
            mask |= MRP_WAYLAND_WINDOW_VISIBLE_MASK;
            win->visible = u->visible;
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_RAISE_MASK)) {
        if ((u->raise && !win->raise) || (!u->raise && win->raise)) {
            mask |= MRP_WAYLAND_WINDOW_RAISE_MASK;
            win->raise = u->raise;
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_MAPPED_MASK)) {
        if ((u->mapped && !win->mapped) || (!u->mapped && win->mapped)) {
            mask |= MRP_WAYLAND_WINDOW_MAPPED_MASK;
            win->mapped = u->mapped;
        }
    }

    if ((u->mask & MRP_WAYLAND_WINDOW_ACTIVE_MASK)) {
        if ((u->active && !win->active) || (!u->active && win->active)) {
            mask |= MRP_WAYLAND_WINDOW_ACTIVE_MASK;
            win->active = u->active;
        }
    }

    return mask;
}

static char *active_str(mrp_wayland_active_t active, char *buf, size_t len)
{
#define PRINT(fmt, args... ) \
    if (p < e) { p += snprintf(p, e-p, " " fmt , ## args); }

    typedef struct {
        mrp_wayland_active_t mask;
        const char *name;
    } map_t;

    static map_t map[] = {
        { MRP_WAYLAND_WINDOW_ACTIVE_POINTER,  "pointer"  },
        { MRP_WAYLAND_WINDOW_ACTIVE_KEYBOARD, "keyboard" },
        { MRP_WAYLAND_WINDOW_ACTIVE_SELECTED, "selected" },
        {                 0,                      NULL   }
    };

    map_t *m;
    char *p, *e;

    e = (p = buf) + len;

    if (!active) {
        PRINT("<none>");
    }
    else {
        for (m = map;   active && m->name;   m++) {
            if ((active & m->mask)) {
                PRINT("%s", m->name);
                active &= ~m->mask;
            }
        }

        if (active)
            PRINT("<unknown 0x%x>", active);
    }

    return buf;

#undef PRINT
}
