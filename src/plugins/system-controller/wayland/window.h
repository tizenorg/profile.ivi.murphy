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

#ifndef __MURPHY_WAYLAND_WINDOW_H__
#define __MURPHY_WAYLAND_WINDOW_H__

#include <sys/types.h>

#include "wayland/layer.h"

enum mrp_wayland_active_e {
    MRP_WAYLAND_WINDOW_ACTIVE_NONE = 0,
    MRP_WAYLAND_WINDOW_ACTIVE_POINTER = 1,
    MRP_WAYLAND_WINDOW_ACTIVE_KEYBOARD = 2,
    MRP_WAYLAND_WINDOW_ACTIVE_SELECTED = 4,
};

enum mrp_wayland_window_operation_e {
    MRP_WAYLAND_WINDOW_OPERATION_NONE = 0,
    MRP_WAYLAND_WINDOW_CREATE,       /* 1 */
    MRP_WAYLAND_WINDOW_DESTROY,      /* 2 */
    MRP_WAYLAND_WINDOW_NAMECHANGE,   /* 3 */
    MRP_WAYLAND_WINDOW_VISIBLE,      /* 4 */
    MRP_WAYLAND_WINDOW_CONFIGURE,    /* 5 */
    MRP_WAYLAND_WINDOW_ACTIVE,       /* 6 */
    MRP_WAYLAND_WINDOW_MAP,          /* 7 */
    MRP_WAYLAND_WINDOW_HINT,         /* 8 */
};


struct mrp_wayland_window_s {
    mrp_wayland_window_manager_t *wm;
    int32_t surfaceid;
    char *name;

    char *appid;
    pid_t pid;

    int32_t nodeid;
    mrp_wayland_layer_t *layer;
    int32_t x, y;
    int32_t width, height;
    double opacity;
    bool visible;
    bool raise;
    bool mapped;
    mrp_wayland_active_t active;
    mrp_wayland_layer_type_t layertype;

    mrp_application_t *application;
    mrp_wayland_area_t *area;
    mrp_wayland_window_map_t *map;

    void *scripting_data;
};


struct mrp_wayland_window_map_s {
    uint32_t type;
    uint32_t target;
    int32_t width, height;
    int32_t stride;
    int32_t format;

    void *scripting_data;
};


enum mrp_wayland_window_update_mask_e {
    MRP_WAYLAND_WINDOW_SURFACEID_MASK   = 0x00001,
    MRP_WAYLAND_WINDOW_NAME_MASK        = 0x00002,
    MRP_WAYLAND_WINDOW_APPID_MASK       = 0x00004,
    MRP_WAYLAND_WINDOW_PID_MASK         = 0x00008,
    MRP_WAYLAND_WINDOW_NODEID_MASK      = 0x00010,
    MRP_WAYLAND_WINDOW_LAYER_MASK       = 0x00020,
    MRP_WAYLAND_WINDOW_X_MASK           = 0x00040,
    MRP_WAYLAND_WINDOW_Y_MASK           = 0x00080,
    MRP_WAYLAND_WINDOW_POSITION_MASK    = 0x000C0,
    MRP_WAYLAND_WINDOW_WIDTH_MASK       = 0x00100,
    MRP_WAYLAND_WINDOW_HEIGHT_MASK      = 0x00200,
    MRP_WAYLAND_WINDOW_SIZE_MASK        = 0x00300,
    MRP_WAYLAND_WINDOW_OPACITY_MASK     = 0x00400,
    MRP_WAYLAND_WINDOW_VISIBLE_MASK     = 0x00800,
    MRP_WAYLAND_WINDOW_RAISE_MASK       = 0x01000,
    MRP_WAYLAND_WINDOW_MAPPED_MASK      = 0x02000,
    MRP_WAYLAND_WINDOW_ACTIVE_MASK      = 0x04000,
    MRP_WAYLAND_WINDOW_LAYERTYPE_MASK   = 0x08000,
    MRP_WAYLAND_WINDOW_APP_MASK         = 0x10000,
    MRP_WAYLAND_WINDOW_AREA_MASK        = 0x20000,
    MRP_WAYLAND_WINDOW_MAP_MASK         = 0x40000,

    MRP_WAYLAND_WINDOW_END_MASK         = 0x80000
};


struct mrp_wayland_window_update_s {
    mrp_wayland_window_update_mask_t mask;
    int32_t surfaceid;
    const char *name;
    const char *appid;
    pid_t pid;
    int32_t nodeid;
    mrp_wayland_layer_t *layer;
    int32_t x, y;
    int32_t width, height;
    double opacity;
    bool visible;
    bool raise;
    bool mapped;
    mrp_wayland_active_t active;
    mrp_wayland_layer_type_t layertype;
    mrp_wayland_area_t *area;
    mrp_wayland_window_map_t *map;
};

mrp_wayland_window_t *
mrp_wayland_window_create(mrp_wayland_t *wl, mrp_wayland_window_update_t *u);

void mrp_wayland_window_destroy(mrp_wayland_window_t *win);

mrp_wayland_window_t *mrp_wayland_window_find(mrp_wayland_t *wl,
                                              int32_t surfaceid);

void mrp_wayland_window_request(mrp_wayland_t *wl,
                                mrp_wayland_window_update_t *u,
                                mrp_wayland_animation_t *anims,
                                uint32_t framerate);

void mrp_wayland_window_update(mrp_wayland_window_t *win,
                               mrp_wayland_window_operation_t oper,
                               mrp_wayland_window_update_t *u);

void mrp_wayland_window_hint(mrp_wayland_window_t *win,
                             mrp_wayland_window_operation_t oper,
                             mrp_wayland_window_update_t *u);

size_t mrp_wayland_window_print(mrp_wayland_window_t *win,
                                mrp_wayland_window_update_mask_t mask,
                                char *buf,
                                size_t len);
size_t mrp_wayland_window_request_print(mrp_wayland_window_update_t *u,
                                        char *buf, size_t len);

const char *mrp_wayland_window_update_mask_str(
                                        mrp_wayland_window_update_mask_t mask);

char *mrp_wayland_window_map_print(mrp_wayland_window_map_t *m,
                                   char *buf, size_t len);

void mrp_wayland_window_set_scripting_data(mrp_wayland_window_t *win,
                                           void *data);

#endif /* __MURPHY_WAYLAND_WINDOW_H__ */
