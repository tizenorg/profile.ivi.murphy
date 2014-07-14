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

#ifndef __MURPHY_WAYLAND_OUTPUT_H__
#define __MURPHY_WAYLAND_OUTPUT_H__

#include "wayland/wayland.h"

enum mrp_wayland_output_operation_e {
    MRP_WAYLAND_OUTPUT_OPERATION_NONE = 0,
    MRP_WAYLAND_OUTPUT_CREATE,       /* 1 */
    MRP_WAYLAND_OUTPUT_DESTROY,      /* 2 */
    MRP_WAYLAND_OUTPUT_CONFIGURE,    /* 3 */
    MRP_WAYLAND_OUTPUT_MODE,         /* 4 */
    MRP_WAYLAND_OUTPUT_DONE,         /* 5 */
    MRP_WAYLAND_OUTPUT_USER_REQUEST, /* 6 */
};


struct mrp_wayland_output_s {
    MRP_WAYLAND_OBJECT_COMMON;

    uint32_t index;
    int32_t outputid;
    char *outputname;

    int32_t pixel_x, pixel_y;
    int32_t physical_width, physical_height;
    int32_t pixel_width, pixel_height;
    int32_t width, height;      /* pixel measures after rotation & flip */
    int32_t subpixel;
    char *make;
    char *model;
    uint32_t rotate;
    bool flip;
    int32_t refresh;

    bool initialized;

    void *scripting_data;
};

enum mrp_wayland_output_update_mask_e {
    MRP_WAYLAND_OUTPUT_INDEX_MASK      = 0x00001,
    MRP_WAYLAND_OUTPUT_OUTPUTID_MASK   = 0x00002,
    MRP_WAYLAND_OUTPUT_NAME_MASK       = 0x00004,
    MRP_WAYLAND_OUTPUT_PIXEL_X_MASK = 0x00008,
    MRP_WAYLAND_OUTPUT_PIXEL_Y_MASK = 0x00010,
    MRP_WAYLAND_OUTPUT_PIXEL_POSITION_MASK  = 0x00018,
    MRP_WAYLAND_OUTPUT_PHYSICAL_WIDTH_MASK  = 0x00020,
    MRP_WAYLAND_OUTPUT_PHYSICAL_HEIGHT_MASK = 0x00040,
    MRP_WAYLAND_OUTPUT_PHYSICAL_SIZE_MASK   = 0x00060,
    MRP_WAYLAND_OUTPUT_PIXEL_WIDTH_MASK     = 0x00080,
    MRP_WAYLAND_OUTPUT_PIXEL_HEIGHT_MASK    = 0x00100,
    MRP_WAYLAND_OUTPUT_PIXEL_SIZE_MASK      = 0x00180,
    MRP_WAYLAND_OUTPUT_WIDTH_MASK       = 0x00200,
    MRP_WAYLAND_OUTPUT_HEIGHT_MASK      = 0x00400,
    MRP_WAYLAND_OUTPUT_SIZE_MASK        = 0x00600,
    MRP_WAYLAND_OUTPUT_SUBPIXEL_MASK    = 0x00800,
    MRP_WAYLAND_OUTPUT_MAKE_MASK        = 0x01000,
    MRP_WAYLAND_OUTPUT_MODEL_MASK       = 0x02000,
    MRP_WAYLAND_OUTPUT_MONITOR_MASK     = 0x03000,
    MRP_WAYLAND_OUTPUT_ROTATE_MASK      = 0x04000,
    MRP_WAYLAND_OUTPUT_FLIP_MASK        = 0x08000,
    MRP_WAYLAND_OUTPUT_REFRESH_MASK     = 0x10000,

    MRP_WAYLAND_OUTPUT_END_MASK         = 0x20000
};

struct mrp_wayland_output_update_s {
    mrp_wayland_output_update_mask_t mask; 
    uint32_t index;
    int32_t outputid;
    const char *outputname;
};

bool mrp_wayland_output_register(mrp_wayland_t *wl);

mrp_wayland_output_t *mrp_wayland_output_find_by_index(mrp_wayland_t *wl,
                                                       uint32_t index);
mrp_wayland_output_t *mrp_wayland_output_find_by_id(mrp_wayland_t *wl,
                                                    int32_t id);

void mrp_wayland_output_request(mrp_wayland_t *wl,
                                mrp_wayland_output_update_t *u);

size_t mrp_wayland_output_print(mrp_wayland_output_t *out,
                                mrp_wayland_output_update_mask_t mask,
                                char *buf, size_t len);

size_t mrp_wayland_output_request_print(mrp_wayland_output_update_t *u,
                                        char *buf, size_t len);

const char *mrp_wayland_output_update_mask_str(
                                        mrp_wayland_output_update_mask_t mask);

void mrp_wayland_output_set_scripting_data(mrp_wayland_output_t *out,
                                           void *data);

#endif /* __MURPHY_WAYLAND_OUTPUT_H__ */
