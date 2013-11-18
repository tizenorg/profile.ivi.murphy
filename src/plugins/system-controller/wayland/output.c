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

#include "output.h"

static bool output_constructor(mrp_wayland_t *wl, mrp_wayland_object_t *obj);

static void geometry_callback(void *, struct wl_output *,
                              int32_t,int32_t, int32_t,int32_t, int32_t,
                              const char *, const char *, int32_t);
static void mode_callback(void *, struct wl_output *, uint32_t,
                          int32_t,int32_t, int32_t);
static void done_callback(void *, struct wl_output *);
static void scale_callback(void *, struct wl_output *, int32_t);
static bool output_constructor(mrp_wayland_t *, mrp_wayland_object_t *);


bool mrp_wayland_output_register(mrp_wayland_t *wl)
{
    mrp_wayland_factory_t factory;

    factory.size = sizeof(mrp_wayland_output_t);
    factory.interface = &wl_output_interface;
    factory.constructor = output_constructor;
    factory.destructor = NULL;

    mrp_wayland_register_interface(wl, &factory);

    return true;
}

static bool output_constructor(mrp_wayland_t *wl, mrp_wayland_object_t *obj)
{
    static struct wl_output_listener listener =  {
        .geometry = geometry_callback,
        .mode     = mode_callback,
        .done     = done_callback,
        .scale    = scale_callback
    };

    MRP_UNUSED(wl);

    mrp_wayland_output_t *out = (mrp_wayland_output_t *)obj;
    int sts;

    MRP_ASSERT(out, "invalid argument");

    sts = wl_output_add_listener((struct wl_output *)out->proxy,&listener,out);

    return (sts < 0) ? false : true;
}

static void geometry_callback(void *data,
                              struct wl_output *wl_output,
                              int32_t x,
                              int32_t y,
                              int32_t physical_width,
                              int32_t physical_height,
                              int32_t subpixel,
                              const char *make,
                              const char *model,
                              int32_t transform)
{
    mrp_wayland_output_t *out = (mrp_wayland_output_t *)data;

    MRP_UNUSED(transform);

    MRP_ASSERT(out, "invalid argument");
    MRP_ASSERT(wl_output == (struct wl_output *)out->proxy,
               "confused with data structures");

    mrp_free(out->make);
    mrp_free(out->model);

    out->x = x;
    out->y = y;
    out->physical_width = physical_width;
    out->physical_height = physical_height;
    out->subpixel = subpixel;
    out->make = mrp_strdup(make ? make : "<unknown>");
    out->model = mrp_strdup(model ? model : "<unknown>");

    mrp_debug("output %u geometry:\n"
              "      position: %d,%d\n"
              "      physical size: %d,%d\n"
              "      subpixel: %d\n"
              "      make : '%s'\n"
              "      model: '%s'",
              out->name,
              out->x, out->y,
              out->physical_width, out->physical_height,
              out->subpixel,
              out->make,
              out->model);
}

static void mode_callback(void *data,
                          struct wl_output *wl_output,
                          uint32_t flags,
                          int32_t width,
                          int32_t height,
                          int32_t refresh)
{
    mrp_wayland_output_t *out = (mrp_wayland_output_t *)data;

    MRP_UNUSED(flags);
    MRP_UNUSED(width);
    MRP_UNUSED(height);
    MRP_UNUSED(refresh);

    MRP_ASSERT(out, "invalid argument");
    MRP_ASSERT(wl_output == (struct wl_output *)out->proxy,
               "confused with data structures");

    mrp_debug("output %u mode:",
              out->name);
}

static void done_callback(void *data,
                          struct wl_output *wl_output)
{
    mrp_wayland_output_t *out = (mrp_wayland_output_t *)data;

    MRP_ASSERT(out, "invalid argument");
    MRP_ASSERT(wl_output == (struct wl_output *)out->proxy,
               "confused with data structures");

    mrp_debug("output %u is initialised", out->name);
}

static void scale_callback(void *data,
                           struct wl_output *wl_output,
                           int32_t factor)
{
    mrp_wayland_output_t *out = (mrp_wayland_output_t *)data;

    MRP_UNUSED(factor);

    MRP_ASSERT(out, "invalid argument");
    MRP_ASSERT(wl_output == (struct wl_output *)out->proxy,
               "confused with data structures");

    mrp_debug("output %u scale:",
              out->name);
}
