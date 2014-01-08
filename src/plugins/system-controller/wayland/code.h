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
#ifndef __MURPHY_WAYLAND_CODE_H__
#define __MURPHY_WAYLAND_CODE_H__

#include "wayland/wayland.h"

enum mrp_wayland_code_operation_e {
    MRP_WAYLAND_CODE_OPERATION_NONE = 0,
    MRP_WAYLAND_CODE_CREATE,       /* 1 */
    MRP_WAYLAND_CODE_DESTROY,      /* 2 */
    MRP_WAYLAND_CODE_STATE_CHANGE, /* 3 */
};

struct mrp_wayland_code_s {
    mrp_wayland_input_t *input;

    int32_t id;
    char *name;
 
    uint32_t time;
    int32_t state;

    void *scripting_data;
};

enum mrp_wayland_code_update_mask_e {
    MRP_WAYLAND_CODE_DEVICE_MASK = 0x0001,
    MRP_WAYLAND_CODE_INPUT_MASK  = 0x0002,
    MRP_WAYLAND_CODE_ID_MASK     = 0x0004,
    MRP_WAYLAND_CODE_NAME_MASK   = 0x0008,
    MRP_WAYLAND_CODE_TIME_MASK   = 0x0010,
    MRP_WAYLAND_CODE_STATE_MASK  = 0x0020,

    MRP_WAYLAND_CODE_END_MASK    = 0x0040
};

struct mrp_wayland_code_update_s {
    mrp_wayland_code_update_mask_t mask;
    const char *device;
    int32_t input;
    int32_t id;
    const char *name;
    uint32_t time;
    int32_t state;
};


mrp_wayland_code_t *mrp_wayland_code_create(mrp_wayland_t *wl,
                                    mrp_wayland_code_update_t *u);
mrp_wayland_code_t *mrp_wayland_code_find(mrp_wayland_t *wl,
                                    const char *device_name,
                                    int32_t input_id,
                                    int32_t code_id);
void mrp_wayland_code_update(mrp_wayland_code_t *code,
                                    mrp_wayland_code_operation_t oper,
                                    mrp_wayland_code_update_t *u);
size_t mrp_wayland_code_print(mrp_wayland_code_t *code,
                                    mrp_wayland_code_update_mask_t mask,
                                    char *buf, size_t len);
size_t mrp_wayland_code_print_indent(mrp_wayland_code_t *code,
                                    mrp_wayland_code_update_mask_t mask,
                                    char *buf, size_t len);
const char *mrp_wayland_code_update_mask_str(
                                    mrp_wayland_code_update_mask_t mask);
void mrp_wayland_code_set_scripting_data(mrp_wayland_code_t *code,
                                    void *data);

#endif /* __MURPHY_WAYLAND_CODE_H__ */
