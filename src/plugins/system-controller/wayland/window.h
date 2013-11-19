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

#include "wayland.h"

enum mrp_wayland_active_e {
    MRP_WAYLAND_WINDOW_ACTIVE_NONE = 0,
    MRP_WAYLAND_WINDOW_ACTIVE_POINTER = 1,
    MRP_WAYLAND_WINDOW_ACTIVE_KEYBOARD = 2,
    MRP_WAYLAND_WINDOW_ACTIVE_SELECTED = 4,
};

enum mrp_wayland_window_operation_e {
    MRP_WAYLAND_WINDOW_OPERATION_NONE = 0,
    MRP_WAYLAND_WINDOW_CREATE,
    MRP_WAYLAND_WINDOW_DESTROY,
    MRP_WAYLAND_WINDOW_NAMECHANGE,
    MRP_WAYLAND_WINDOW_VISIBLE,
    MRP_WAYLAND_WINDOW_CONFIGURE,
    MRP_WAYLAND_WINDOW_ACTIVE
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
    bool visible;
    bool raise;
    bool mapped;

    mrp_wayland_active_t active;
};


enum mrp_wayland_window_update_mask_e {
    MRP_WAYLAND_WINDOW_SURFACEID_MASK   = 0x0001,
    MRP_WAYLAND_WINDOW_NAME_MASK        = 0x0002,
    MRP_WAYLAND_WINDOW_APPID_MASK       = 0x0004,
    MRP_WAYLAND_WINDOW_PID_MASK         = 0x0008,
    MRP_WAYLAND_WINDOW_NODEID_MASK      = 0x0010,
    MRP_WAYLAND_WINDOW_LAYER_MASK       = 0x0020,
    MRP_WAYLAND_WINDOW_X_MASK           = 0x0040,
    MRP_WAYLAND_WINDOW_Y_MASK           = 0x0080,
    MRP_WAYLAND_WINDOW_POSITION_MASK    = 0x00C0,
    MRP_WAYLAND_WINDOW_WIDTH_MASK       = 0x0100,
    MRP_WAYLAND_WINDOW_HEIGHT_MASK      = 0x0200,
    MRP_WAYLAND_WINDOW_SIZE_MASK        = 0x0300,
    MRP_WAYLAND_WINDOW_VISIBLE_MASK     = 0x0400,
    MRP_WAYLAND_WINDOW_RAISE_MASK       = 0x0800,
    MRP_WAYLAND_WINDOW_ACTIVE_MASK      = 0x1000,
    MRP_WAYLAND_WINDOW_MAPPED_MASK      = 0x2000,

    MRP_WAYLAND_WINDOW_END_MASK         = 0x4000
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
    bool visible;
    bool raise;
    bool mapped;
    mrp_wayland_active_t active;
};

mrp_wayland_window_t *
mrp_wayland_window_create(mrp_wayland_t *wl, mrp_wayland_window_update_t *u);

void mrp_wayland_window_destroy(mrp_wayland_window_t *win);

mrp_wayland_window_t *mrp_wayland_window_find(mrp_wayland_t *wl,
                                              int32_t surfaceid);

void mrp_wayland_window_visibility_request(mrp_wayland_window_t *win,
                                           int32_t visible, int32_t raise,
                                           const char *animation_name,
                                           int32_t animation_time);
void mrp_wayland_window_active_request(mrp_wayland_window_t *win,
                                       mrp_wayland_active_t active);
void mrp_wayland_window_map_request(mrp_wayland_window_t *win,
                                    bool map, uint32_t framerate);
void mrp_wayland_window_geometry_request(mrp_wayland_window_t *win,
                                         int32_t nodeid,
                                         int32_t x, int32_t y,
                                         int32_t width, int32_t height,
                                         const char *move_animation,
                                         int32_t move_time,
                                         const char *resize_animation,
                                         int32_t resize_time);
void mrp_wayland_window_layer_request(mrp_wayland_window_t *win,
                                      int32_t layerid);

void mrp_wayland_window_request(mrp_wayland_t *wl,
                                mrp_wayland_window_update_t *u,
                                mrp_wayland_animation_t *anims,
                                uint32_t framerate);

void mrp_wayland_window_update(mrp_wayland_window_t *win,
                               mrp_wayland_window_operation_t oper,
                               mrp_wayland_window_update_t *u);

size_t mrp_wayland_window_print(mrp_wayland_window_t *win,
                                mrp_wayland_window_update_mask_t mask,
                                char *buf,
                                size_t len);
size_t mrp_wayland_window_request_print(mrp_wayland_window_update_t *u,
                                        mrp_wayland_window_update_mask_t mask,
                                        char *buf,
                                        size_t len);

const char *mrp_wayland_window_update_mask_str(
                                        mrp_wayland_window_update_mask_t);

#endif /* __MURPHY_WAYLAND_WINDOW_H__ */
