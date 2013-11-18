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

#ifndef __MURPHY_WAYLAND_AREA_H__
#define __MURPHY_WAYLAND_AREA_H__

#include <sys/types.h>

#include "wayland/wayland.h"

enum mrp_wayland_area_operation_e {
    MRP_WAYLAND_AREA_OPERATION_NONE = 0,
    MRP_WAYLAND_AREA_CREATE,
    MRP_WAYLAND_AREA_DESTROY,
};

enum mrp_wayland_area_align_e {
    MRP_WAYLAND_AREA_ALIGN_MIDDLE   = 0x00,

    MRP_WAYLAND_AREA_ALIGN_HMASK    = 0x03,
    MRP_WAYLAND_AREA_ALIGN_LEFT     = 0x01,
    MRP_WAYLAND_AREA_ALIGN_RIGHT    = 0x02,

    MRP_WAYLAND_AREA_ALIGN_VMASK    = 0x0C,
    MRP_WAYLAND_AREA_ALIGN_TOP      = 0x04,
    MRP_WAYLAND_AREA_ALIGN_BOTTOM   = 0x08,

    MRP_WAYLAND_AREA_ALIGN_END_MASK = 0x10
};

struct mrp_wayland_area_s {
    mrp_wayland_t *wl;
    mrp_wayland_window_manager_t *wm;

    int32_t areaid;
    char *name;
    char *fullname;
    mrp_wayland_output_t *output;
    int32_t x, y;
    int32_t width, height;
    bool keepratio;
    mrp_wayland_area_align_t align;

    void *scripting_data;
};


enum mrp_wayland_area_update_mask_e {
    MRP_WAYLAND_AREA_AREAID_MASK    = 0x0001,
    MRP_WAYLAND_AREA_NAME_MASK      = 0x0002,
    MRP_WAYLAND_AREA_FULLNAME_MASK  = 0x0004,
    MRP_WAYLAND_AREA_OUTPUT_MASK    = 0x0008,
    MRP_WAYLAND_AREA_X_MASK         = 0x0010,
    MRP_WAYLAND_AREA_Y_MASK         = 0x0020,
    MRP_WAYLAND_AREA_POSITION_MASK  = 0x0030,
    MRP_WAYLAND_AREA_WIDTH_MASK     = 0x0040,
    MRP_WAYLAND_AREA_HEIGHT_MASK    = 0x0080,
    MRP_WAYLAND_AREA_SIZE_MASK      = 0x00C0,
    MRP_WAYLAND_AREA_KEEPRATIO_MASK = 0x0100,
    MRP_WAYLAND_AREA_ALIGN_MASK     = 0x0200,

    MRP_WAYLAND_AREA_END_MASK       = 0x0400
};


struct mrp_wayland_area_update_s {
    mrp_wayland_area_update_mask_t mask;
    int32_t areaid;
    const char *name;
    mrp_wayland_output_t *output;
    int32_t x, y;
    int32_t width, height;
    bool keepratio;
    mrp_wayland_area_align_t align;
};

mrp_wayland_area_t *mrp_wayland_area_create(mrp_wayland_t *wl,
                                            mrp_wayland_area_update_t *u);
void mrp_wayland_area_destroy(mrp_wayland_area_t *area);

mrp_wayland_area_t *mrp_wayland_area_find(mrp_wayland_t *wl,
                                          const char *fullname);

size_t mrp_wayland_area_print(mrp_wayland_area_t *area,
                              mrp_wayland_area_update_mask_t mask,
                              char *buf, size_t len);

const char *mrp_wayland_area_align_str(mrp_wayland_area_align_t align);

void mrp_wayland_area_set_scripting_data(mrp_wayland_area_t *area, void *data);


#endif /* __MURPHY_WAYLAND_AREA_H__ */
