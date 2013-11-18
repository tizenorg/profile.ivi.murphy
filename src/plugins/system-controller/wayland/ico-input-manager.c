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
#include <ico-uxf-weston-plugin/ico_input_mgr-client-protocol.h>

#include "ico-input-manager.h"
#include "input-manager.h"

typedef struct exinput_s  exinput_t;
typedef struct device_s   device_t;

struct mrp_ico_input_manager_s {
    MRP_WAYLAND_INPUT_MANAGER_COMMON;
};

struct exinput_s {
    MRP_WAYLAND_OBJECT_COMMON;
};

struct device_s {
    MRP_WAYLAND_OBJECT_COMMON;
};


static bool input_manager_constructor(mrp_wayland_t *, mrp_wayland_object_t *);
static bool exinput_constructor(mrp_wayland_t *, mrp_wayland_object_t *);
static void exinput_destructor(mrp_wayland_object_t *);
static bool device_constructor(mrp_wayland_t *, mrp_wayland_object_t *);
static void device_destructor(mrp_wayland_object_t *);


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


bool mrp_ico_input_manager_register(mrp_wayland_t *wl)
{
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

    return true;
}

static bool input_manager_constructor(mrp_wayland_t *wl,
                                      mrp_wayland_object_t *obj)
{
    mrp_ico_input_manager_t *im = (mrp_ico_input_manager_t *)obj;

    MRP_ASSERT(im, "invalid argument");

    wl->im = (mrp_wayland_input_manager_t *)im;

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

    sts = ico_input_mgr_device_add_listener((struct ico_input_mgr_device *)dev,
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
    exinput_t *xinp = (exinput_t *)data;

    MRP_ASSERT(xinp && xinp->interface && xinp->interface->wl,
               "invalid argument");
    MRP_ASSERT(ico_exinput == (struct ico_exinput *)xinp->proxy,
               "confused with data structures");

    mrp_debug("exinput_capabilities_callback(device='%s', type=%d,"
              "swname='%s', input=%d, codename='%s', code=%d)",
              device, type, swname, input, codename, code);

}


static void exinput_code_callback(void *data,
                                  struct ico_exinput *ico_exinput,
                                  const char *device,
                                  int32_t input,
                                  const char *codename,
                                  int32_t code)
{
    exinput_t *xinp = (exinput_t *)data;

    MRP_ASSERT(xinp && xinp->interface && xinp->interface->wl,
               "invalid argument");
    MRP_ASSERT(ico_exinput == (struct ico_exinput *)xinp->proxy,
               "confused with data structures");

    mrp_debug("exinput_code_callback(device='%s', input=%d, "
              "codename='%s', code=%d)",
              device, input, codename, code);
}


static void exinput_input_callback(void *data,
                                   struct ico_exinput *ico_exinput,
                                   uint32_t time,
                                   const char *device,
                                   int32_t input,
                                   int32_t code,
                                   int32_t state)
{
    exinput_t *xinp = (exinput_t *)data;

    MRP_ASSERT(xinp && xinp->interface && xinp->interface->wl,
               "invalid argument");
    MRP_ASSERT(ico_exinput == (struct ico_exinput *)xinp->proxy,
               "confused with data structures");

    mrp_debug("exinput_input_callback(time=%u, device='%s', "
              "input=%d, code=%d, state=%d)",
              time, device, input, code, state);
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
