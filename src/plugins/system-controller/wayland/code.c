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

#include "code.h"
#include "input.h"
#include "input-manager.h"
#include "scripting-wayland.h"


static mrp_wayland_code_update_mask_t update(mrp_wayland_code_t *,
                                             mrp_wayland_code_update_t*);

static size_t print_code(mrp_wayland_code_t *,
                         mrp_wayland_code_update_mask_t,
                         const char *, char *, size_t);


mrp_wayland_code_t *mrp_wayland_code_create(mrp_wayland_t *wl,
                                            mrp_wayland_code_update_t *u)
{
    mrp_wayland_code_update_mask_t mask;
    mrp_wayland_input_device_t *dev;
    mrp_wayland_input_t *inp;
    mrp_wayland_code_t *code;
    bool has_name;
    size_t i;
    char buf[4096];

    mask = MRP_WAYLAND_CODE_DEVICE_MASK |
           MRP_WAYLAND_CODE_INPUT_MASK  |
           MRP_WAYLAND_CODE_ID_MASK     ;

    MRP_ASSERT(wl && u && (u->mask & mask) == mask && u->device,
               "invalid argument");

    has_name = (u->mask & MRP_WAYLAND_CODE_NAME_MASK) ? true : false;

    MRP_ASSERT(!has_name || (has_name && u->name), "invalid argument");

    if (!(inp = mrp_wayland_input_find_by_name_and_id(wl,u->device,u->input))||
        !(dev = inp->device))
    {
        mrp_log_error("system-controller: failed to create input code "
                      "'%s'/%d/%d: can't find/create input device",
                      u->device, u->input, u->id);
        return NULL;
    }


    for (i = 0;  i < inp->ncode;  i++) {
        code = inp->codes + i;

        if (u->id == code->id) {
            if ((!has_name && code->name                                  ) ||
                ( has_name && (!code->name || strcmp(u->name, code->name)))  )
            {
                mrp_log_error("system-controller: attempt the redefine code "
                              "with different name '%s'/%d/%d",
                              u->device, u->input, u->id);
                return NULL;
            }
            else {
                mrp_debug("code '%s'/%d/%d was already defined",
                          u->device, u->input, u->id);
                return code;
            }
        }
    } /* for */

    i = inp->ncode++;
    inp->codes = mrp_reallocz(inp->codes, i, inp->ncode);

    MRP_ASSERT(inp->codes, "can't allocate memory for code");

    code = inp->codes + i;

    memset(code, 0, sizeof(*code));
    code->input = inp;
    code->id    = u->id;
    code->name  = has_name ? mrp_strdup(u->name) : NULL;

    if (!mrp_htbl_insert(dev->codes, &code->id, code)) {
        mrp_log_error("system-controller: failed to create input code "
                      "'%s'/%d/%d: code for device already exists",
                      u->device, u->input, u->id);
        memset(code, 0, sizeof(*code));
        inp->ncode--;
        return NULL;
    }

    if (wl->create_scripting_inputs) {
        code->scripting_data = mrp_wayland_scripting_code_create_from_c(NULL,
                                                                        code);
    }

    mask |= update(code, u);

    mrp_wayland_code_print(code, mask, buf,sizeof(buf));
    mrp_debug("input code %d created%s", code->id, buf);

    return code;
}

mrp_wayland_code_t *mrp_wayland_code_find(mrp_wayland_t *wl,
                                          const char *devnam,
                                          int32_t input_id,
                                          int32_t code_id)
{
    mrp_wayland_input_t *inp;
    mrp_wayland_code_t *code;
    size_t i;

    MRP_ASSERT(wl && devnam && input_id >= 0, "invalid argument");

    if ((inp = mrp_wayland_input_find_by_name_and_id(wl, devnam, input_id))) {
        for (i = 0;  i < inp->ncode;  i++) {
            code = inp->codes + i;

            if (code_id == code->id)
                return code;
        }
    }

    return NULL;
}

void mrp_wayland_code_update(mrp_wayland_code_t *code,
                             mrp_wayland_code_operation_t oper,
                             mrp_wayland_code_update_t *u)
{
    mrp_wayland_code_update_mask_t mask;
    mrp_wayland_t *wl;
    char buf[4096];

    MRP_ASSERT(code && u, "invalid argument");

    if ((u->mask & MRP_WAYLAND_CODE_DEVICE_MASK)) {
        if (!u->device || strcmp(u->device, code->input->device->name)) {
            mrp_log_error("system-controller: attempt to change of device "
                          "to '%s' of code %d",
                          u->device ? u->device : "<unknown>", code->id);
            return;
        }
    }
    if ((u->mask & MRP_WAYLAND_CODE_INPUT_MASK)) {
        if (u->input != code->input->id) {
            mrp_log_error("system-controller: attempt to change of input "
                          "to %d of code %d", u->input, code->id);
            return;
        }
    }
    if ((u->mask & MRP_WAYLAND_CODE_ID_MASK)) {
        if (u->id != code->id) {
            mrp_log_error("system-controller: attempt to change of id "
                          "to %d of input %d", u->id, code->id);
            return;
        }
    }
    if ((u->mask & MRP_WAYLAND_CODE_NAME_MASK)) {
        if (!u->name  || strcmp(u->name, code->name)) {
            mrp_log_error("system-controller: attempt to change of code name "
                          "to '%s' of code %d",
                          u->name ? u->name : "<unknown>", code->id);
            return;
        }
    }


    mask = update(code, u);

    if (!mask)
        mrp_debug("code %d update requested but nothing changed", code->id);
    else {
        mrp_wayland_code_print(code, mask, buf,sizeof(buf));
        mrp_debug("code %d updated%s", code->id, buf);

        if (code->input->device->im && code->input->device->im->interface &&
            code->input->device->im->interface->wl)
        {
            wl = code->input->device->im->interface->wl;

            if (wl->code_update_callback)
                wl->code_update_callback(wl, oper, mask, code);
        }
    }
}

size_t mrp_wayland_code_print(mrp_wayland_code_t *code,
                                    mrp_wayland_code_update_mask_t mask,
                                    char *buf, size_t len)
{
    return print_code(code, mask, "", buf, len);
}

size_t mrp_wayland_code_print_indent(mrp_wayland_code_t *code,
                                     mrp_wayland_code_update_mask_t mask,
                                     char *buf, size_t len)
{
    return print_code(code, mask, "      ", buf, len);
}


const char *mrp_wayland_code_update_mask_str(
                                           mrp_wayland_code_update_mask_t mask)
{
    switch (mask) {
    case MRP_WAYLAND_CODE_DEVICE_MASK:  return "device";
    case MRP_WAYLAND_CODE_INPUT_MASK:   return "input";
    case MRP_WAYLAND_CODE_ID_MASK:      return "id";
    case MRP_WAYLAND_CODE_NAME_MASK:    return "name";
    case MRP_WAYLAND_CODE_TIME_MASK:    return "time";
    case MRP_WAYLAND_CODE_STATE_MASK:   return "state";
    default:                            return "<unknown>";
    }
}


void mrp_wayland_code_set_scripting_data(mrp_wayland_code_t *code, void *data)
{
    MRP_ASSERT(code, "Invalid Argument");

    mrp_debug("%sset scripting data", data ? "" : "re");

    code->scripting_data = data;
}


static mrp_wayland_code_update_mask_t update(mrp_wayland_code_t *code,
                                             mrp_wayland_code_update_t *u)
{
    mrp_wayland_input_update_mask_t mask = 0;

    if ((u->mask & MRP_WAYLAND_CODE_TIME_MASK)) {
        if (u->time != code->time) {
            mask |= MRP_WAYLAND_CODE_TIME_MASK;
            code->time = u->time;
        }
    }
    if ((u->mask & MRP_WAYLAND_CODE_STATE_MASK)) {
        if (u->state != code->state) {
            mask |= MRP_WAYLAND_CODE_STATE_MASK;
            code->state = u->state;
        }
    }

    return mask;
}

static size_t print_code(mrp_wayland_code_t *code,
                         mrp_wayland_code_update_mask_t mask,
                         const char *gap, char *buf, size_t len)
{
#define PRINT(fmt, args...)                                             \
    if (p < e) { p += snprintf(p, e-p, "\n      %s" fmt , gap, ## args); }

    char *p, *e;

    e = (p = buf) + len;

    *p = 0;

    if ((mask & MRP_WAYLAND_CODE_DEVICE_MASK)) {
        if (!code->input || !code->input->device ||
            !code->input->device->name)
        {
            PRINT("device: <unknown>");
        }
        else {
            PRINT("device: '%s'", code->input->device->name);
        }
    }
    if ((mask & MRP_WAYLAND_CODE_INPUT_MASK)) {
        if (!code->input) {
            PRINT("input: <unknown>");
        }
        else if (!code->input->name) {
            PRINT("input: %d - <unknown>", code->input->id);
        }
        else {
            PRINT("input: %d - '%s'", code->input->id, code->input->name);
        }
    }
    if ((mask & MRP_WAYLAND_CODE_NAME_MASK))
        PRINT("name: '%s'", code->name);
    if ((mask & MRP_WAYLAND_CODE_TIME_MASK))
        PRINT("time: %u", code->time);
    if ((mask & MRP_WAYLAND_CODE_STATE_MASK))
        PRINT("state: %d", code->state);
    
    return p - buf;

#undef PRINT
}
