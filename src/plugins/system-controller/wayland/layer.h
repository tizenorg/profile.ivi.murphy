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

#ifndef __MURPHY_WAYLAND_LAYER_H__
#define __MURPHY_WAYLAND_LAYER_H__

#include <sys/types.h>

#include "wayland/wayland.h"

enum mrp_wayland_layer_type_e {
    MRP_WAYLAND_LAYER_TYPE_UNKNOWN  = 0,
    MRP_WAYLAND_LAYER_BACKGROUND,  /* 1 */
    MRP_WAYLAND_LAYER_APPLICATION, /* 2 */
    MRP_WAYLAND_LAYER_INPUT,       /* 3 */
    MRP_WAYLAND_LAYER_TOUCH,       /* 4 */
    MRP_WAYLAND_LAYER_CURSOR,      /* 5 */
    MRP_WAYLAND_LAYER_STARTUP,     /* 6 */

    MRP_WAYLAND_LAYER_TYPE_MAX
};

enum mrp_wayland_layer_operation_e {
    MRP_WAYLAND_LAYER_OPERATION_NONE = 0,
    MRP_WAYLAND_LAYER_CREATE,
    MRP_WAYLAND_LAYER_DESTROY,
    MRP_WAYLAND_LAYER_VISIBLE
};

struct mrp_wayland_layer_s {
    mrp_wayland_t *wl;
    mrp_wayland_window_manager_t *wm;

    int32_t layerid;
    char *name;
    mrp_wayland_layer_type_t type;
    bool visible;

    void *scripting_data;
};


enum mrp_wayland_layer_update_mask_e {
    MRP_WAYLAND_LAYER_LAYERID_MASK   = 0x0001,
    MRP_WAYLAND_LAYER_NAME_MASK      = 0x0002,
    MRP_WAYLAND_LAYER_TYPE_MASK      = 0x0004,
    MRP_WAYLAND_LAYER_VISIBLE_MASK   = 0x0008,

    MRP_WAYLAND_LAYER_END_MASK       = 0x0010
};


struct mrp_wayland_layer_update_s {
    mrp_wayland_layer_update_mask_t mask;
    int32_t layerid;
    const char *name;
    mrp_wayland_layer_type_t type;
    bool visible;
};

mrp_wayland_layer_t *mrp_wayland_layer_create(mrp_wayland_t *wl,
                                              mrp_wayland_layer_update_t *u);
void mrp_wayland_layer_destroy(mrp_wayland_layer_t *layer);

mrp_wayland_layer_t *mrp_wayland_layer_find(mrp_wayland_t *wl,
                                            int32_t layerid);

void mrp_wayland_layer_visibility_request(mrp_wayland_layer_t *layer,
                                          int32_t visible);

void mrp_wayland_layer_request(mrp_wayland_t *wl,
                               mrp_wayland_layer_update_t *u);
void mrp_wayland_layer_update(mrp_wayland_layer_t *layer,
                              mrp_wayland_layer_operation_t oper,
                              mrp_wayland_layer_update_t *u);

size_t mrp_wayland_layer_print(mrp_wayland_layer_t *layer,
                               mrp_wayland_layer_update_mask_t mask,
                               char *buf,
                               size_t len);
size_t mrp_wayland_layer_request_print(mrp_wayland_layer_update_t *u,
                                       char *buf, size_t len);

const char *mrp_wayland_layer_update_mask_str(mrp_wayland_layer_update_mask_t
                                                                         mask);
const char *mrp_wayland_layer_type_str(mrp_wayland_layer_type_t type);

void mrp_wayland_layer_set_scripting_data(mrp_wayland_layer_t *layer,
                                          void *data);


#endif /* __MURPHY_WAYLAND_LAYER_H__ */
