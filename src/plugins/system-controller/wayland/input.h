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
#ifndef __MURPHY_WAYLAND_INPUT_H__
#define __MURPHY_WAYLAND_INPUT_H__

#include "wayland/wayland.h"

enum mrp_wayland_input_operation_e {
    MRP_WAYLAND_INPUT_OPERATION_NONE = 0,
    MRP_WAYLAND_INPUT_CREATE,       /* 1 */
    MRP_WAYLAND_INPUT_DESTROY,      /* 2 */
    MRP_WAYLAND_INPUT_UPDATE,       /* 3 */
};

enum mrp_wayland_input_type_e {
    MRP_WAYLAND_INPUT_TYPE_UNKNOWN = 0,
    MRP_WAYLAND_INPUT_TYPE_POINTER,
    MRP_WAYLAND_INPUT_TYPE_KEYBOARD,
    MRP_WAYLAND_INPUT_TYPE_TOUCH,
    MRP_WAYLAND_INPUT_TYPE_SWITCH,
    MRP_WAYLAND_INPUT_TYPE_HAPTIC,

    MRP_WAYLAND_INPUT_TYPE_MAX
};

struct mrp_wayland_input_device_s {
    mrp_wayland_input_manager_t *im;
    char *name;
    int32_t id;
    struct {
        mrp_htbl_t *by_id;
        mrp_htbl_t *by_name;
    } inputs;
    mrp_htbl_t *codes;
};

struct mrp_wayland_input_s {
    mrp_wayland_input_device_t *device;
    mrp_wayland_input_type_t type;
    int32_t id;
    char *name;
    int32_t keycode;

    size_t ncode;
    mrp_wayland_code_t *codes;

    bool permanent;
    char *appid;

    bool connected;

    void *scripting_data;
};


enum mrp_wayland_input_update_mask_e {
    MRP_WAYLAND_INPUT_DEVICE_MASK      = 0x0001,
    MRP_WAYLAND_INPUT_DEVICE_NAME_MASK = 0x0002,
    MRP_WAYLAND_INPUT_DEVICE_ID_MASK   = 0x0004,
    MRP_WAYLAND_INPUT_TYPE_MASK        = 0x0008,
    MRP_WAYLAND_INPUT_ID_MASK          = 0x0010,
    MRP_WAYLAND_INPUT_NAME_MASK        = 0x0020,
    MRP_WAYLAND_INPUT_KEYCODE_MASK     = 0x0040,
    MRP_WAYLAND_INPUT_CODES_MASK       = 0x0080,
    MRP_WAYLAND_INPUT_PERMANENT_MASK   = 0x0100,
    MRP_WAYLAND_INPUT_APPID_MASK       = 0x0200,
    MRP_WAYLAND_INPUT_CONNECTED_MASK   = 0x0400,

    MRP_WAYLAND_INPUT_END_MASK         = 0x0800
};

struct mrp_wayland_input_update_s {
    mrp_wayland_input_update_mask_t mask;
    struct {
        const char *name;
        int32_t id;
    } device;
    mrp_wayland_input_type_t type;
    int32_t id;
    const char *name;
    int32_t keycode;
    bool permanent;
    const char *appid;
    bool connected;
};

struct mrp_wayland_input_event_s {
    struct {
        mrp_wayland_input_update_mask_t mask;
        union {
            char *name;
            int32_t id;
        };
    } device;
    int32_t codeid;

    int32_t surfaceid;
    uint32_t time;
    int32_t value;

};


mrp_wayland_input_t *mrp_wayland_input_create(mrp_wayland_t *wl,
                                              mrp_wayland_input_update_t *u);

mrp_wayland_input_t *mrp_wayland_input_find_by_name_and_id(mrp_wayland_t *wl,
                                                           const char *devnam,
                                                           int32_t inpid);
mrp_wayland_input_t *mrp_wayland_input_find_by_name(mrp_wayland_t *wl,
                                                    const char *devnam,
                                                    const char *inpnam);
mrp_wayland_input_t *mrp_wayland_input_find_by_id(mrp_wayland_t *wl,
                                                  int32_t devid,
                                                  int32_t inpid);

void mrp_wayland_send_input(mrp_wayland_t *wl,
                            mrp_wayland_input_event_t *ev);
void mrp_wayland_input_request(mrp_wayland_t *wl,
                               mrp_wayland_input_update_t *u);
void mrp_wayland_input_update(mrp_wayland_input_t *inp,
                              mrp_wayland_input_operation_t oper,
                              mrp_wayland_input_update_t *u);
size_t mrp_wayland_input_print(mrp_wayland_input_t *inp,
                               mrp_wayland_code_update_mask_t mask,
                               char *buf, size_t len);
size_t mrp_wayland_input_request_print(mrp_wayland_input_update_t *u,
                                       char *buf, size_t len);
const char *mrp_wayland_input_type_str(mrp_wayland_input_type_t type);
const char *mrp_wayland_input_update_mask_str(
                                   mrp_wayland_input_update_mask_t mask);
void mrp_wayland_input_set_scripting_data(mrp_wayland_input_t *inp,void *data);

#endif /* __MURPHY_WAYLAND_INPUT_H__ */
