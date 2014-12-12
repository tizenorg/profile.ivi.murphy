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

#include "input.h"
#include "code.h"
#include "input-manager.h"
#include "scripting-wayland.h"


static mrp_wayland_input_update_mask_t update(mrp_wayland_input_t *,
                                              mrp_wayland_input_update_t *);
static void print_if_connected(mrp_wayland_input_t *,
                               mrp_wayland_input_update_mask_t);
static void free_input(void *, void *);

static mrp_wayland_input_device_t *get_input_device(mrp_wayland_t *,
                                                    const char *, bool *);
static void destroy_input_device(mrp_wayland_t *,mrp_wayland_input_device_t *);

static uint32_t inp_id_hash(const void *);
static uint32_t code_id_hash(const void *);
static int id_compare(const void *, const void *);


mrp_wayland_input_t *mrp_wayland_input_create(mrp_wayland_t *wl,
                                              mrp_wayland_input_update_t *u)
{
    mrp_wayland_input_update_mask_t mask;
    mrp_wayland_input_device_t *dev;
    mrp_wayland_input_t *inp;
    bool created;
    char buf[4096];

    mask = MRP_WAYLAND_INPUT_DEVICE_NAME_MASK |
           MRP_WAYLAND_INPUT_DEVICE_ID_MASK   |
           MRP_WAYLAND_INPUT_TYPE_MASK        |
           MRP_WAYLAND_INPUT_ID_MASK          ;

    MRP_ASSERT(wl && u && (u->mask & mask) == mask &&
               u->device.name && u->device.id >= 0 &&
               u->type > 0 && u->type < MRP_WAYLAND_INPUT_TYPE_MAX,
               "invalid argument");

    if (!(dev = get_input_device(wl, u->device.name, &created))) {
        mrp_log_error("system-controller: failed to create input %d: "
                      "can't find/create input device '%s'",
                      u->id, u->device.name);
        return NULL;
    }

    if (!(inp = mrp_allocz(sizeof(mrp_wayland_input_t)))) {
        mrp_log_error("system-controller: failed to create input %d: "
                      "can't allocate memory", u->id);
        return NULL;
    }

    if (created) {
        dev->id = u->device.id;
        if (!mrp_htbl_insert(wl->devices.by_id, &dev->id, dev)) {
            mrp_log_error("system-controller: failed to create input %d: "
                          "device id %d already exists",
                          u->id, u->device.id);
            destroy_input_device(wl, dev);
            return NULL;
        }
    }

    inp->device = dev;
    inp->type = u->type;
    inp->id = u->id;

    if (!(mrp_htbl_insert(dev->inputs.by_id, &inp->id, inp))) {
        mrp_log_error("system-controller: failed to create input %d: "
                      "input already exists", u->id);
        mrp_free(inp);
        return NULL;
    }

    if (wl->create_scripting_inputs) {
        inp->scripting_data = mrp_wayland_scripting_input_create_from_c(NULL,
                                                                        inp);
    }

    mask |= update(inp, u);

    mrp_wayland_input_print(inp, mask, buf,sizeof(buf));
    mrp_debug("input %d created%s", inp->id, buf);

    print_if_connected(inp, mask);

    if (wl->input_update_callback)
        wl->input_update_callback(wl, MRP_WAYLAND_INPUT_CREATE, mask, inp);

    return inp;
}

mrp_wayland_input_t *mrp_wayland_input_find_by_name_and_id(mrp_wayland_t *wl,
                                                           const char *devnam,
                                                           int32_t inpid)
{
    mrp_wayland_input_device_t *dev;
    mrp_wayland_input_t *inp = NULL;

    MRP_ASSERT(wl && inpid >= 0, "invalid argument");

    if (devnam) {
        dev = (mrp_wayland_input_device_t*)mrp_htbl_lookup(wl->devices.by_name,
                                                           (void *)devnam);
        if (dev) {
            inp = (mrp_wayland_input_t *)mrp_htbl_lookup(dev->inputs.by_id,
                                                         (void *)&inpid);
        }
    }

    return inp;
}

mrp_wayland_input_t *mrp_wayland_input_find_by_name(mrp_wayland_t *wl,
                                                    const char *devnam,
                                                    const char *inpnam)
{
    mrp_wayland_input_device_t *dev;
    mrp_wayland_input_t *inp = NULL;

    MRP_ASSERT(wl && devnam, "invalid argument");

    dev = (mrp_wayland_input_device_t *)mrp_htbl_lookup(wl->devices.by_name,
                                                        (void *)devnam);
    if (inpnam && dev) {
        inp = (mrp_wayland_input_t *)mrp_htbl_lookup(dev->inputs.by_name,
                                                     (void *)inpnam);
    }

    return inp;
}


mrp_wayland_input_t *mrp_wayland_input_find_by_id(mrp_wayland_t *wl,
                                                  int32_t devid,
                                                  int32_t inpid)
{
    mrp_wayland_input_device_t *dev;
    mrp_wayland_input_t *inp = NULL;

    MRP_ASSERT(wl && devid >= 0 && inpid >= 0, "invalid argument");

    dev = (mrp_wayland_input_device_t *)mrp_htbl_lookup(wl->devices.by_id,
                                                        (void *)&inpid);
    if (dev) {
        inp = (mrp_wayland_input_t *)mrp_htbl_lookup(dev->inputs.by_id,
                                                     (void *)&inpid);
    }

    return inp;
}

void mrp_wayland_send_input(mrp_wayland_t *wl, mrp_wayland_input_event_t *ev)
{
    mrp_wayland_input_manager_t *im;
    mrp_wayland_input_device_t *dev;
    mrp_wayland_code_t *code;
    char dbuf[256];

    MRP_ASSERT(wl && ev, "Invalid argument");

    if ((ev->device.mask & MRP_WAYLAND_INPUT_DEVICE_ID_MASK)) {
        dev = (mrp_wayland_input_device_t*)mrp_htbl_lookup(wl->devices.by_id,
                                                           &ev->device.id);
        snprintf(dbuf, sizeof(dbuf), "%d", ev->device.id);
    }
    else if ((ev->device.mask & MRP_WAYLAND_INPUT_DEVICE_NAME_MASK)) {
        dev = (mrp_wayland_input_device_t*)mrp_htbl_lookup(wl->devices.by_name,
                                                           ev->device.name);
        snprintf(dbuf, sizeof(dbuf), "'%s'", ev->device.name);
    }
    else {
        dev = NULL;
        snprintf(dbuf, sizeof(dbuf), "<invalid device>");
    }

    mrp_debug("device=%s code=%d surface=%d time=%u value=%d",
              dbuf, ev->codeid, ev->surfaceid, ev->time, ev->value);

    if (!dev) {
        mrp_debug("can't send input: invalid device %s", dbuf);
        return;
    }

    if (!(im = dev->im)) {
        mrp_debug("can't send input: input manager missing");
        return;
    }
    if (!(code=(mrp_wayland_code_t*)mrp_htbl_lookup(dev->codes,&ev->codeid))) {
        mrp_debug("can't send input: invalid code %d for device %s",
                  ev->codeid, dbuf);
        return;
    }

    im->send_input(code, ev->surfaceid, ev->time, ev->value);
}

void mrp_wayland_input_request(mrp_wayland_t *wl,mrp_wayland_input_update_t *u)
{
    mrp_wayland_input_t *inp;
    mrp_wayland_input_manager_t *im;
    char rbuf[4096], inbuf[128];

    MRP_ASSERT(wl && u, "invalid argument");

    if (!(u->mask & MRP_WAYLAND_INPUT_DEVICE_NAME_MASK) || !u->device.name) {
        mrp_debug("can't find input: device <unknown>");
        return;
    }

    if ((u->mask & MRP_WAYLAND_INPUT_ID_MASK)) {
        inp = mrp_wayland_input_find_by_name_and_id(wl, u->device.name, u->id);

        if (!inp) {
            mrp_debug("can't find input: device:'%s' id:%d",
                      u->device.name, u->id);
            return;
        }

        snprintf(inbuf, sizeof(inbuf), "%d", u->id);
    }
    else if ((u->mask & MRP_WAYLAND_INPUT_NAME_MASK) && u->name) {
        inp = mrp_wayland_input_find_by_name(wl, u->device.name, u->name);
        if (!inp) {
            mrp_debug("can't find input: device:'%s' name:'%s'",
                      u->device.name, u->name);
            return;
        }
        snprintf(inbuf, sizeof(inbuf), "'%s'", u->name);
    }
    else {
        mrp_debug("can't find input: device:'%s'; "
                  "neither id nor name is present", u->device.name);
        return;
    }

    MRP_ASSERT(inp->device && inp->device->im,"confused with data structures");
    im = inp->device->im;

    mrp_wayland_input_request_print(u, rbuf,sizeof(rbuf));
    mrp_debug("input '%s'/%s%s", u->device.name, inbuf, rbuf);

    im->input_request(inp, u);
}

void mrp_wayland_input_update(mrp_wayland_input_t *inp,
                              mrp_wayland_input_operation_t oper,
                              mrp_wayland_input_update_t *u)
{
    mrp_wayland_t *wl;
    mrp_wayland_input_update_mask_t mask;
    char buf[4096];

    MRP_ASSERT(inp && inp->device && inp->device->name && u,
               "invalid argument");

    if ((u->mask & MRP_WAYLAND_INPUT_DEVICE_NAME_MASK)) {
        if (!u->device.name || strcmp(u->device.name, inp->device->name)) {
            mrp_log_error("system-controller: attempt to change of device "
                          "name to '%s' of input %d", u->device.name, inp->id);
            return;
        }
    }
    if ((u->mask & MRP_WAYLAND_INPUT_DEVICE_ID_MASK)) {
        if (!inp->device || u->device.id !=  inp->device->id) {
            mrp_log_error("system-controller: attempt to change of device "
                          "id to %d of input %d", u->device.id, inp->id);
            return;
        }
    }
    if ((u->mask & MRP_WAYLAND_INPUT_TYPE_MASK)) {
        if (u->type != inp->type) {
            mrp_log_error("system-controller: attempt to change of device "
                          "type to '%s' of input %d",
                          mrp_wayland_input_type_str(u->type), inp->id);
            return;
        }
    }
    if ((u->mask & MRP_WAYLAND_INPUT_ID_MASK)) {
        if (u->id != inp->id) {
            mrp_log_error("system-controller: attempt to change of device "
                          "id to %d of input %d", u->id, inp->id);
            return;
        }
    }

    mask = update(inp, u);

    if (!mask)
        mrp_debug("input %d update requested but nothing changed", inp->id);
    else {
        mrp_wayland_input_print(inp, mask, buf,sizeof(buf));
        mrp_debug("input %d updated%s", inp->id, buf);

        print_if_connected(inp, mask);

        if (inp->device->im && inp->device->im->interface &&
            inp->device->im->interface->wl)
        {
            wl = inp->device->im->interface->wl;

            if (wl->input_update_callback)
                wl->input_update_callback(wl, oper, mask, inp);
        }
    }
}

size_t mrp_wayland_input_print(mrp_wayland_input_t *inp,
                               mrp_wayland_code_update_mask_t mask,
                               char *buf, size_t len)
{
#define PRINT(fmt, args...) \
    if (p < e) { p += snprintf(p, e-p, "\n      " fmt , ## args); }

    static mrp_wayland_code_update_mask_t code_mask =
        MRP_WAYLAND_CODE_ID_MASK   |
        MRP_WAYLAND_CODE_NAME_MASK ;

    mrp_wayland_code_t *code;
    char codbuf[1024];
    char *p, *e;
    size_t i;

    e = (p = buf) + len;

    *p = 0;

    if ((mask & MRP_WAYLAND_INPUT_DEVICE_MASK)) {
        if (!inp->device || !inp->device->name) {
            PRINT("device: <unknown>");
        }
        else {
            PRINT("device: '%s'", inp->device->name);
        }
    }

    if ((mask & MRP_WAYLAND_INPUT_TYPE_MASK)) {
        PRINT("type: %d - %s", inp->type,
              mrp_wayland_input_type_str(inp->type));
    }
    if ((mask & MRP_WAYLAND_INPUT_ID_MASK))
        PRINT("id: %d", inp->id);
    if ((mask & MRP_WAYLAND_INPUT_NAME_MASK))
        PRINT("name: '%s'", inp->name);
    if ((mask & MRP_WAYLAND_INPUT_KEYCODE_MASK))
        PRINT("keycode: %d - 0x%x", inp->keycode, inp->keycode);
    if ((mask & MRP_WAYLAND_INPUT_PERMANENT_MASK))
        PRINT("permanent: %s", inp->permanent ? "yes":"no");
    if ((mask & MRP_WAYLAND_INPUT_APPID_MASK))
        PRINT("appid: '%s'", inp->appid ? inp->appid : "<not-set>");
    if ((mask & MRP_WAYLAND_INPUT_CONNECTED_MASK))
        PRINT("connected: %s", inp->connected ? "yes":"no");

    if ((mask & MRP_WAYLAND_INPUT_CODES_MASK)) {
        if (!inp->ncode) {
            PRINT("no codes");
        }
        else {
            for (i = 0;  i < inp->ncode;  i++) {
                code = inp->codes + i;
                mrp_wayland_code_print_indent(code, code_mask,
                                              codbuf, sizeof(codbuf));
                PRINT("code %d:%s", code->id, codbuf);
            }
        }
    }
    
    return p - buf;
    
#undef PRINT
}

size_t mrp_wayland_input_request_print(mrp_wayland_input_update_t *u,
                                       char *buf, size_t len)
{
#define PRINT(fmt, args...) \
    if (p < e) { p += snprintf(p, e-p, "\n      " fmt , ## args); }

    char *p, *e;

    e = (p = buf) + len;

    *p = 0;

    if ((u->mask & MRP_WAYLAND_INPUT_DEVICE_NAME_MASK))
        PRINT("device.name: '%s'",u->device.name ? u->device.name:"<unknown>");
    if ((u->mask & MRP_WAYLAND_INPUT_DEVICE_ID_MASK))
        PRINT("device.id: %d", u->device.id);
    if ((u->mask & MRP_WAYLAND_INPUT_TYPE_MASK))
        PRINT("type: %d - %s", u->type, mrp_wayland_input_type_str(u->type));
    if ((u->mask & MRP_WAYLAND_INPUT_ID_MASK))
        PRINT("id: %d", u->id);
    if ((u->mask & MRP_WAYLAND_INPUT_NAME_MASK))
        PRINT("name: '%s'", u->name ? u->name : "<unknown>");
    if ((u->mask & MRP_WAYLAND_INPUT_KEYCODE_MASK))
        PRINT("keycode: %d - 0x%x", u->keycode, u->keycode);
    if ((u->mask & MRP_WAYLAND_INPUT_PERMANENT_MASK))
        PRINT("permanent: %s", u->permanent ? "yes":"no");
    if ((u->mask & MRP_WAYLAND_INPUT_APPID_MASK))
        PRINT("appid: '%s'", u->appid ? u->appid : "<not-set>");
    if ((u->mask & MRP_WAYLAND_INPUT_CONNECTED_MASK))
        PRINT("connected: %s", u->connected ? "yes":"no");
    
    return p - buf;
    
#undef PRINT
}

const char *mrp_wayland_input_type_str(mrp_wayland_input_type_t type)
{
    switch(type) {
    case MRP_WAYLAND_INPUT_TYPE_POINTER:   return "pointer";
    case MRP_WAYLAND_INPUT_TYPE_KEYBOARD:  return "keyboard";
    case MRP_WAYLAND_INPUT_TYPE_TOUCH:     return "touch";
    case MRP_WAYLAND_INPUT_TYPE_SWITCH:    return "switch";
    case MRP_WAYLAND_INPUT_TYPE_HAPTIC:    return "haptic";
    default:                               return "<unknown>";
    }
}

const char *
mrp_wayland_input_update_mask_str(mrp_wayland_input_update_mask_t mask)
{
    switch (mask) {
    case MRP_WAYLAND_INPUT_DEVICE_NAME_MASK: return "device_name";
    case MRP_WAYLAND_INPUT_TYPE_MASK:        return "type";
    case MRP_WAYLAND_INPUT_ID_MASK:          return "id";
    case MRP_WAYLAND_INPUT_NAME_MASK:        return "name";
    case MRP_WAYLAND_INPUT_KEYCODE_MASK:     return "keycode";
    case MRP_WAYLAND_INPUT_CODES_MASK:       return "codes";
    case MRP_WAYLAND_INPUT_PERMANENT_MASK:   return "permanent";
    case MRP_WAYLAND_INPUT_APPID_MASK:       return "appid";
    case MRP_WAYLAND_INPUT_CONNECTED_MASK:   return "connected";
    default:                                 return "<unknown>";
    }
}

void mrp_wayland_input_set_scripting_data(mrp_wayland_input_t *inp, void *data)
{
    MRP_ASSERT(inp, "Invalid Argument");

    mrp_debug("%sset scripting data", data ? "" : "re");

    inp->scripting_data = data;
}

static mrp_wayland_input_update_mask_t update(mrp_wayland_input_t *inp,
                                              mrp_wayland_input_update_t *u)
{
    mrp_wayland_input_update_mask_t mask = 0;
    mrp_wayland_input_device_t *dev = inp->device;
    mrp_wayland_input_manager_t *im;
    mrp_wayland_input_update_t u2;

    if ((u->mask & MRP_WAYLAND_INPUT_NAME_MASK)) {
        if ((!u->name && inp->name) || (u->name && !inp->name) ||
            (u->name && inp->name && strcmp(u->name, inp->name)))
        {
            mask |= MRP_WAYLAND_INPUT_NAME_MASK;
            if (inp->name) {
                if (dev)
                    mrp_htbl_remove(dev->inputs.by_name, inp->name, false);
                mrp_free(inp->name);
                inp->name = NULL;
            }
            if (u->name) {
                if ((inp->name = mrp_strdup(u->name))) {
                    mrp_htbl_insert(dev->inputs.by_name, inp->name, inp);
                }
                else {
                    mrp_log_error("system-controller: can't get memory for "
                                  "input %d", inp->id);
                }
            }
        }
    }

    if ((u->mask & MRP_WAYLAND_INPUT_KEYCODE_MASK)) {
        if (u->keycode != inp->keycode) {
            mask |= MRP_WAYLAND_INPUT_KEYCODE_MASK;
            inp->keycode = u->keycode;
        }
    }

    if ((u->mask & MRP_WAYLAND_INPUT_PERMANENT_MASK)) {
        if ((u->permanent && !inp->permanent) ||
            (!u->permanent && inp->permanent)  )
        {
            mask |= MRP_WAYLAND_INPUT_PERMANENT_MASK;
            inp->permanent = u->permanent;
        }
    }

    if ((u->mask & MRP_WAYLAND_INPUT_APPID_MASK)) {
        if ((u->appid && !inp->appid) || (!u->appid && inp->appid) ||
            (u->appid && inp->appid && strcmp(u->appid, inp->appid)))
        {
            mask |= MRP_WAYLAND_INPUT_APPID_MASK;
            mrp_free(inp->appid);
            inp->appid = u->appid ? mrp_strdup(u->appid) : NULL;
        }
    }

    if ((u->mask & MRP_WAYLAND_INPUT_CONNECTED_MASK)) {
        if ((u->connected && !inp->connected) ||
            (!u->connected && inp->connected)  )
        {
            mask |= MRP_WAYLAND_INPUT_CONNECTED_MASK;
            inp->connected = u->connected;

            if ((im = inp->device->im) && inp->permanent && inp->appid) {
                memset(&u2, 0, sizeof(u2));
                u2.mask = MRP_WAYLAND_INPUT_APPID_MASK     |
                          MRP_WAYLAND_INPUT_KEYCODE_MASK   |
                          MRP_WAYLAND_INPUT_PERMANENT_MASK |
                          MRP_WAYLAND_INPUT_CONNECTED_MASK ;
                u2.permanent = inp->permanent;
                u2.appid     = inp->appid;
                u2.keycode   = inp->keycode;
                u2.connected = inp->connected;

                im->input_request(inp, &u2);
            }
        }
    }

    return mask;
}


static void print_if_connected(mrp_wayland_input_t *inp,
                               mrp_wayland_input_update_mask_t update_mask)
{
    static mrp_wayland_input_update_mask_t print_mask =
        (MRP_WAYLAND_INPUT_END_MASK - 1) & (~MRP_WAYLAND_INPUT_CODES_MASK);

    char buf[4096];

    if ((update_mask & MRP_WAYLAND_INPUT_CONNECTED_MASK) && inp->connected) {
        mrp_wayland_input_print(inp, print_mask, buf,sizeof(buf));
        mrp_log_info("system-controller: input connected%s", buf);
    }
}

static void free_input(void *key, void *object)
{
    mrp_wayland_input_t *inp = (mrp_wayland_input_t *)object;
    mrp_wayland_code_t *code;
    size_t i;

    MRP_UNUSED(key);

    mrp_debug("   destroying input %d - '%s'",
              inp->id, inp->name ? inp->name : "<null>");

    free(inp->name);

    if (inp->ncode && inp->codes) {
        for (i = 0;  i < inp->ncode;  i++) {
            code = inp->codes + i;
            mrp_free(code->name);
        }
        free(inp->codes);
    }
    
    free(inp);
}


static mrp_wayland_input_device_t *get_input_device(mrp_wayland_t *wl,
                                                    const char *name,
                                                    bool *created_ret)
{
    mrp_wayland_input_device_t *dev;
    mrp_htbl_config_t icfg, ncfg, ccfg;

    if (created_ret)
        *created_ret = false;

    if (!wl || !name)
        dev = NULL;
    else {
        dev = (mrp_wayland_input_device_t*)mrp_htbl_lookup(wl->devices.by_name,
                                                           (void *)name);
        if (!dev) {
            if ((dev = mrp_allocz(sizeof(mrp_wayland_input_device_t)))) {
                memset(&icfg, 0, sizeof(icfg));
                icfg.nentry = MRP_WAYLAND_INPUT_MAX;
                icfg.comp = id_compare;
                icfg.hash = inp_id_hash;
                icfg.free = free_input;
                icfg.nbucket = MRP_WAYLAND_INPUT_BUCKETS;
                
                memset(&ncfg, 0, sizeof(ncfg));
                ncfg.nentry = MRP_WAYLAND_INPUT_MAX;
                ncfg.comp = mrp_string_comp;
                ncfg.hash = mrp_string_hash;
                ncfg.nbucket = MRP_WAYLAND_INPUT_BUCKETS;

                memset(&ccfg, 0, sizeof(ccfg));
                ccfg.nentry = MRP_WAYLAND_CODE_MAX;
                ccfg.comp = id_compare;
                ccfg.hash = code_id_hash;
                ccfg.nbucket = MRP_WAYLAND_CODE_BUCKETS;
                
                dev->im = wl->im;
                dev->name = mrp_strdup(name);
                dev->inputs.by_id = mrp_htbl_create(&icfg);
                dev->inputs.by_name = mrp_htbl_create(&ncfg);
                dev->codes = mrp_htbl_create(&ccfg);

                MRP_ASSERT(dev->name, "memory allocation failure");

                mrp_htbl_insert(wl->devices.by_name, dev->name, dev);

                if (created_ret)
                    *created_ret = true;

                mrp_debug("device '%s' was created", dev->name);
            }
        }
    }

    return dev;
}

static void destroy_input_device(mrp_wayland_t *wl,
                                 mrp_wayland_input_device_t *dev)
{
    if (dev && dev->name) {
        mrp_debug("destroying input device '%s'", dev->name);

        mrp_htbl_remove(wl->devices.by_name, dev->name, false);
        mrp_htbl_remove(wl->devices.by_id, &dev->id, false);

        mrp_free(dev->name);
        mrp_htbl_destroy(dev->inputs.by_name, false);
        mrp_htbl_destroy(dev->inputs.by_id, true);
        mrp_htbl_destroy(dev->codes, true);

        mrp_free(dev);
    }
}

static uint32_t inp_id_hash(const void *pkey)
{
    int32_t key = *(int32_t *)pkey;

    return key % MRP_WAYLAND_INPUT_BUCKETS;
}

static uint32_t code_id_hash(const void *pkey)
{
    int32_t key = *(int32_t *)pkey;

    return key % MRP_WAYLAND_CODE_BUCKETS;
}

static int id_compare(const void *pkey1, const void *pkey2)
{
    int32_t key1 = *(int32_t *)pkey1;
    int32_t key2 = *(int32_t *)pkey2;

    return (key1 == key2) ? 0 : ((key1 < key2) ? -1 : 1);
}
