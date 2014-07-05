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

#include <wayland-util.h>
#if 0
#include <ico-uxf-weston-plugin/ico_input_mgr-client-protocol.h>
#endif

#include "glm-input-manager.h"
#include "input-manager.h"
#include "input.h"
#include "code.h"
#include "window.h"

typedef struct exinput_s  exinput_t;
typedef struct device_s   device_t;

struct mrp_glm_input_manager_s {
    MRP_WAYLAND_INPUT_MANAGER_COMMON;
};

struct exinput_s {
    MRP_WAYLAND_OBJECT_COMMON;
};

struct device_s {
    MRP_WAYLAND_OBJECT_COMMON;
};


#if 0
static bool input_manager_constructor(mrp_wayland_t *, mrp_wayland_object_t *);
static bool exinput_constructor(mrp_wayland_t *, mrp_wayland_object_t *);
static void exinput_destructor(mrp_wayland_object_t *);
static bool device_constructor(mrp_wayland_t *, mrp_wayland_object_t *);
static void device_destructor(mrp_wayland_object_t *);

static void code_create(mrp_wayland_t *, const char *,int32_t,
                        const char *,int32_t);

static void input_request(mrp_wayland_input_t *, mrp_wayland_input_update_t *);
static void send_input(mrp_wayland_code_t *, int32_t, uint32_t, int32_t);

static void set_application(mrp_wayland_input_t*, mrp_wayland_input_update_t*);

static mrp_wayland_input_type_t get_input_type(int32_t);


static bool input_manager_constructor(mrp_wayland_t *,mrp_wayland_object_t *);

static void exinput_capabilities_callback(void *, struct ico_exinput *,
                                          const char *, int32_t,
                                          const char *, int32_t,
                                          const char *, int32_t);
static void exinput_code_callback(void *, struct ico_exinput *, const char *,
                                  int32_t, const char *, int32_t);
static void exinput_input_callback(void *, struct ico_exinput *, uint32_t,
                                   const char *, int32_t, int32_t, int32_t);
static void device_input_regions_callback(void *,
                                          struct ico_input_mgr_device *,
                                          struct wl_array *);
#endif


bool mrp_glm_input_manager_register(mrp_wayland_t *wl)
{
#if 0
    mrp_wayland_factory_t factory;

    factory.size = sizeof(mrp_ico_input_manager_t);
    factory.interface = &ico_input_mgr_control_interface;
    factory.constructor = input_manager_constructor;
    factory.destructor = NULL;
    mrp_wayland_register_interface(wl, &factory);

    factory.size = sizeof(exinput_t);
    factory.interface = &ico_exinput_interface;
    factory.constructor = exinput_constructor;
    factory.destructor = exinput_destructor;
    mrp_wayland_register_interface(wl, &factory);

    factory.size = sizeof(device_t);
    factory.interface = &ico_input_mgr_device_interface;
    factory.constructor = device_constructor;
    factory.destructor = device_destructor;
    mrp_wayland_register_interface(wl, &factory);
#endif

    return true;
}

#if 0
static bool input_manager_constructor(mrp_wayland_t *wl,
                                      mrp_wayland_object_t *obj)
{
    mrp_ico_input_manager_t *im = (mrp_ico_input_manager_t *)obj;

    MRP_ASSERT(im, "invalid argument");

    im->input_request = input_request;
    im->send_input = send_input;

    mrp_wayland_register_input_manager(wl, (mrp_wayland_input_manager_t *)im);

    return true;
}

static bool exinput_constructor(mrp_wayland_t *wl, mrp_wayland_object_t *obj)
{
    static struct ico_exinput_listener listener = {
        .capabilities = exinput_capabilities_callback,
        .code         = exinput_code_callback,
        .input        = exinput_input_callback
    };

    exinput_t *xinp = (exinput_t *)obj;
    int sts;

    MRP_UNUSED(wl);

    sts = ico_exinput_add_listener((struct ico_exinput *)xinp->proxy,
                                   &listener, xinp);
    if (sts < 0)
        return false;

    return true;
}

static void exinput_destructor(mrp_wayland_object_t *obj)
{
    exinput_t *xinp = (exinput_t *)obj;

    MRP_UNUSED(xinp);
}

static bool device_constructor(mrp_wayland_t *wl, mrp_wayland_object_t *obj)
{
    static struct ico_input_mgr_device_listener listener = {
        .input_regions = NULL
    };

    device_t *dev = (device_t *)obj;
    int sts;

    MRP_UNUSED(wl);
    MRP_UNUSED(device_input_regions_callback);

    sts = ico_input_mgr_device_add_listener(
                                   (struct ico_input_mgr_device *)dev->proxy,
                                   &listener, dev);
    if (sts < 0)
        return false;

    return true;
}

static void device_destructor(mrp_wayland_object_t *obj)
{
    device_t *dev = (device_t *)obj;

    MRP_UNUSED(dev);
}



static void exinput_capabilities_callback(void *data,
                                          struct ico_exinput *ico_exinput,
                                          const char *device,
                                          int32_t type,
                                          const char *swname,
                                          int32_t input,
                                          const char *codename,
                                          int32_t code)
{
    static int32_t id = 0x10000;

    exinput_t *xinp = (exinput_t *)data;
    mrp_wayland_t *wl;
    mrp_wayland_input_update_t u;
    mrp_wayland_input_t *inp;

    MRP_ASSERT(xinp && xinp->interface && xinp->interface->wl && device,
               "invalid argument");
    MRP_ASSERT(ico_exinput == (struct ico_exinput *)xinp->proxy,
               "confused with data structures");

    wl = xinp->interface->wl;

    mrp_debug("exinput_capabilities_callback(device='%s', type=%d,"
              "swname='%s', input=%d, codename='%s', code=%d)",
              device, type, swname, input, codename, code);

    memset(&u, 0, sizeof(u));
    u.mask      = MRP_WAYLAND_INPUT_DEVICE_NAME_MASK |
                  MRP_WAYLAND_INPUT_TYPE_MASK        |
                  MRP_WAYLAND_INPUT_ID_MASK          |
                  MRP_WAYLAND_INPUT_CONNECTED_MASK   ;
    u.device.name = device;
    u.type        = get_input_type(type);
    u.id          = input;
    u.connected   = true;

    if (swname) {
        u.mask |= MRP_WAYLAND_INPUT_NAME_MASK;
        u.name  = swname;
    }

    if (u.type) {
        if ((inp = mrp_wayland_input_find_by_name_and_id(wl, device, u.id)))
            mrp_wayland_input_update(inp, MRP_WAYLAND_INPUT_UPDATE, &u);
        else {
            u.mask |= MRP_WAYLAND_INPUT_DEVICE_ID_MASK;
            u.device.id = id++;
            mrp_wayland_input_create(wl, &u);
        }

        code_create(wl, device, input, codename, code);
    }
}


static void exinput_code_callback(void *data,
                                  struct ico_exinput *ico_exinput,
                                  const char *device,
                                  int32_t input,
                                  const char *codename,
                                  int32_t code)
{
    exinput_t *xinp = (exinput_t *)data;
    mrp_wayland_t *wl;

    MRP_ASSERT(xinp && xinp->interface && xinp->interface->wl,
               "invalid argument");
    MRP_ASSERT(ico_exinput == (struct ico_exinput *)xinp->proxy,
               "confused with data structures");

    wl = xinp->interface->wl;

    mrp_debug("exinput_code_callback(device='%s', input=%d, "
              "codename='%s', code=%d)",
              device, input, codename, code);

    code_create(wl, device, input, codename, code);
}


static void exinput_input_callback(void *data,
                                   struct ico_exinput *ico_exinput,
                                   uint32_t time,
                                   const char *device,
                                   int32_t input,
                                   int32_t codeid,
                                   int32_t state)
{
    exinput_t *xinp = (exinput_t *)data;
    mrp_wayland_t *wl;
    mrp_wayland_code_t *code;
    mrp_wayland_code_update_t u;

    MRP_ASSERT(xinp && xinp->interface && xinp->interface->wl,
               "invalid argument");
    MRP_ASSERT(ico_exinput == (struct ico_exinput *)xinp->proxy,
               "confused with data structures");

    wl = xinp->interface->wl;

    mrp_debug("exinput_input_callback(time=%u, device='%s', "
              "input=%d, code=%d, state=%d)",
              time, device, input, codeid, state);

    if (!(code = mrp_wayland_code_find(wl, device, input, codeid))) {
        mrp_log_error("system-controller: attempt to update unknown input "
                      "(device='%s' input=%d code=%d)", device, input, codeid);
        return;
    }

    memset(&u, 0, sizeof(u));
    u.mask = MRP_WAYLAND_CODE_DEVICE_MASK |
             MRP_WAYLAND_CODE_INPUT_MASK  |
             MRP_WAYLAND_CODE_ID_MASK     |
             MRP_WAYLAND_CODE_TIME_MASK   |
             MRP_WAYLAND_CODE_STATE_MASK  ;
    u.device = device;
    u.input  = input;
    u.id     = codeid;
    u.time   = time;
    u.state  = state;

    mrp_wayland_code_update(code,  MRP_WAYLAND_CODE_STATE_CHANGE, &u);
}


static void device_input_regions_callback(void *data,
                                          struct ico_input_mgr_device *
                                                              ico_input_device,
                                          struct wl_array *regions)
{
    device_t *dev = (device_t *)data;

    MRP_UNUSED(regions);

    MRP_ASSERT(dev && dev->interface && dev->interface->wl,
               "invalid argument");
    MRP_ASSERT(ico_input_device == (struct ico_input_mgr_device *)dev->proxy,
               "confused with data structures");

    mrp_debug("device_input_regions_callback()");
}

static void code_create(mrp_wayland_t *wl, const char *device, int32_t input,
                        const char *codename, int32_t code)
{
    mrp_wayland_code_update_t u;

    memset(&u, 0, sizeof(u));
    u.mask   = MRP_WAYLAND_CODE_DEVICE_MASK |
               MRP_WAYLAND_CODE_INPUT_MASK  |
               MRP_WAYLAND_CODE_ID_MASK     ;
    u.device = device;
    u.input  = input;
    u.id     = code;

    if (codename) {
        u.mask |= MRP_WAYLAND_CODE_NAME_MASK;
        u.name  = codename;
    }

    mrp_wayland_code_create(wl, &u);
}

static void set_application(mrp_wayland_input_t *inp,
                            mrp_wayland_input_update_t *u)
{
    mrp_wayland_input_manager_t *im;
    struct ico_input_mgr_control *ico_input_mgr_control;
    const char *device;
    int32_t input;
    int32_t fix;
    int32_t keycode;
    const char *add_appid, *delete_appid;
    mrp_wayland_input_update_t u2;

    if (!inp || !inp->device || !inp->device->im)
        return;

    if (!inp->connected)
        return;

    im = inp->device->im;
    ico_input_mgr_control = (struct ico_input_mgr_control *)im->proxy;

    device  = inp->device->name;
    input   = inp->id;
    fix     = ((u->mask & MRP_WAYLAND_INPUT_PERMANENT_MASK) ?
                u->permanent : inp->permanent) ? 1 : 0;
    keycode = (u->mask & MRP_WAYLAND_INPUT_KEYCODE_MASK) ? 
                u->keycode : inp->keycode;

    add_appid = delete_appid = NULL;

    if ((u->mask & MRP_WAYLAND_INPUT_APPID_MASK) && u->appid && u->appid[0])
        add_appid = u->appid;

    if (!(u->mask & MRP_WAYLAND_INPUT_CONNECTED_MASK))
        delete_appid = inp->appid;


    if (!add_appid && !delete_appid) {
        mrp_debug("nothing to do");
        return;
    }

    if (add_appid && delete_appid) {
        if (!strcmp(add_appid, delete_appid) &&
            fix == (inp->permanent ? 1 : 0)  &&
            keycode == inp->keycode)
        {
            mrp_debug("nothing to do");
            return;
        }
    }

    if (delete_appid) {
        mrp_debug("calling ico_input_mgr_control_del_input_app"
                  "(appid='%s', device='%s', input=%d)",
                  delete_appid, device, input);

        ico_input_mgr_control_del_input_app(ico_input_mgr_control,delete_appid,
                                            device, input);
    }

    if (add_appid) {
        mrp_debug("calling ico_input_mgr_control_add_input_app"
                  "(appid='%s', device='%s', input=%d, fix=%d, keycode=%d)",
                  add_appid, device, input, fix, keycode);

        ico_input_mgr_control_add_input_app(ico_input_mgr_control, add_appid,
                                            device, input, fix, keycode);
    }

    memset(&u2, 0, sizeof(u2));
    u2.mask  = u->mask & (MRP_WAYLAND_INPUT_APPID_MASK |
                          MRP_WAYLAND_INPUT_PERMANENT_MASK);
    u2.appid = add_appid;
    u2.permanent = u->permanent;

    mrp_wayland_input_update(inp, MRP_WAYLAND_INPUT_UPDATE, &u2);
}

static void input_request(mrp_wayland_input_t *inp,
                          mrp_wayland_input_update_t *u)
{
    static mrp_wayland_input_update_mask_t application_mask =
        MRP_WAYLAND_INPUT_APPID_MASK     |
        MRP_WAYLAND_INPUT_PERMANENT_MASK ;

    mrp_wayland_input_update_mask_t mask;
    mrp_wayland_t *wl;
    char buf[4096];

    MRP_ASSERT(inp && u, "invalid argument");

    mrp_wayland_input_request_print(u, buf,sizeof(buf));
    mrp_debug("request for input update:%s", buf);

    if (inp->device->im && inp->device->im->interface &&
        inp->device->im->interface->wl)
    {
        wl = inp->device->im->interface->wl;
        mask = u->mask;

        while (mask) {
            if ((mask & application_mask)) {
                set_application(inp, u);
                mask &= ~application_mask;
            }
            else {
                mask = 0;
            }
        }

        mrp_wayland_flush(wl);
    }
}

static void send_input(mrp_wayland_code_t *code,
                       int32_t surfaceid,
                       uint32_t time,
                       int32_t value)
{
    mrp_wayland_t *wl;
    mrp_wayland_input_manager_t *im;
    struct ico_input_mgr_control *ico_input_mgr_control;
    mrp_wayland_input_t *inp;
    mrp_wayland_input_device_t *device;
    mrp_wayland_window_t *win;
    int32_t type;

    MRP_ASSERT(code, "invalid argument");

    inp = code->input;
    device = inp->device;
    im = device->im;
    wl = im->interface->wl;
    ico_input_mgr_control = (struct ico_input_mgr_control *)im->proxy;

    switch (inp->type) {
    case MRP_WAYLAND_INPUT_TYPE_POINTER:
        type = ICO_INPUT_MGR_DEVICE_TYPE_POINTER;
        break;
    case MRP_WAYLAND_INPUT_TYPE_KEYBOARD:
        type = MRP_WAYLAND_INPUT_TYPE_KEYBOARD;
        break;
    case MRP_WAYLAND_INPUT_TYPE_TOUCH:
        type = MRP_WAYLAND_INPUT_TYPE_TOUCH;
        break;
    case MRP_WAYLAND_INPUT_TYPE_SWITCH:
        type = ICO_INPUT_MGR_DEVICE_TYPE_SWITCH;
        break;
    case MRP_WAYLAND_INPUT_TYPE_HAPTIC:
        type = ICO_INPUT_MGR_DEVICE_TYPE_HAPTIC;
        break;
    default:
        mrp_debug("can't send input event: invalid type %d", inp->type);
        return;
    }


    if (!(win = mrp_wayland_window_find(wl, surfaceid))) {
        mrp_debug("can't send input event: invalid surface %d", surfaceid);
        return;
    }

    mrp_debug("calling ico_input_mgr_control_send_input_event"
              "(target='%s', surfaceid=%d, type=%d, deviceno=%d,"
              " time=%u, code=%d, value=%d)",
              win->appid, win->surfaceid, type, device->id,
              time, code->id, value);
        
    ico_input_mgr_control_send_input_event(ico_input_mgr_control, win->appid,
                                           win->surfaceid, type, device->id,
                                           time, code->id, value);
}


static mrp_wayland_input_type_t get_input_type(int32_t type)
{
    switch (type) {

    case ICO_INPUT_MGR_DEVICE_TYPE_POINTER: 
        return MRP_WAYLAND_INPUT_TYPE_POINTER;

    case ICO_INPUT_MGR_DEVICE_TYPE_KEYBOARD:
        return MRP_WAYLAND_INPUT_TYPE_KEYBOARD;

    case ICO_INPUT_MGR_DEVICE_TYPE_TOUCH:
        return MRP_WAYLAND_INPUT_TYPE_TOUCH;

    case ICO_INPUT_MGR_DEVICE_TYPE_SWITCH:
        return MRP_WAYLAND_INPUT_TYPE_SWITCH;

    case ICO_INPUT_MGR_DEVICE_TYPE_HAPTIC:
        return MRP_WAYLAND_INPUT_TYPE_HAPTIC;

    default:
        return MRP_WAYLAND_INPUT_TYPE_UNKNOWN;
    }
}
#endif
