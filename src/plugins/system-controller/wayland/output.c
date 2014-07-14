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
#include "area.h"
#include "scripting-wayland.h"

typedef struct {
    mrp_wayland_output_t *output;
    int count;
} output_iterator_helper_t;

static bool output_constructor(mrp_wayland_t *, mrp_wayland_object_t *);

static void geometry_callback(void *, struct wl_output *,
                              int32_t,int32_t, int32_t,int32_t, int32_t,
                              const char *, const char *, int32_t);
static void mode_callback(void *, struct wl_output *, uint32_t,
                          int32_t,int32_t, int32_t);
static void done_callback(void *, struct wl_output *);
static void scale_callback(void *, struct wl_output *, int32_t);

static void check_if_ready(mrp_wayland_t *, mrp_wayland_output_t *);
static int print_area(void *, void *, void *);


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

mrp_wayland_output_t *mrp_wayland_output_find_by_index(mrp_wayland_t *wl,
                                                       uint32_t index)
{    
    MRP_ASSERT(wl, "invalid argument");

    return (mrp_wayland_output_t *)mrp_htbl_lookup(wl->outputs.by_index,
                                                   &index);
}

mrp_wayland_output_t *mrp_wayland_output_find_by_id(mrp_wayland_t *wl,
                                                    int32_t id)
{    
    MRP_ASSERT(wl, "invalid argument");

    return (mrp_wayland_output_t *)mrp_htbl_lookup(wl->outputs.by_id, &id);
}

void mrp_wayland_output_request(mrp_wayland_t *wl,
                                mrp_wayland_output_update_t *u)
{
    mrp_wayland_output_t *out;
    mrp_wayland_output_update_mask_t mask;
    int32_t old_id;
    char buf[4096];

    MRP_ASSERT(wl && u, "invalid arguments");

    if (!(u->mask & MRP_WAYLAND_OUTPUT_INDEX_MASK) ||
        !(out = mrp_wayland_output_find_by_index(wl, u->index)))
    {
        mrp_debug("can't find output %u: request rejected", u->index);
        return;
    }

    mrp_wayland_output_request_print(u, buf, sizeof(buf));
    mrp_debug("request for output %u update:%s", out->index, buf);

    mask = 0;

    if ((u->mask & MRP_WAYLAND_OUTPUT_OUTPUTID_MASK)) {
        if (u->outputid < 0) {
            mrp_log_error("system-controller: refuse to set output ID %d "
                          "(invalid id)", u->outputid);
        }
        else {
            if (u->outputid != out->outputid) {
                mask |= MRP_WAYLAND_OUTPUT_OUTPUTID_MASK;

                if ((old_id = out->outputid) >= 0)
                    mrp_htbl_remove(wl->outputs.by_id, &out->outputid, false);

                out->outputid = u->outputid;

                if (!mrp_htbl_insert(wl->outputs.by_id, &out->outputid, out)) {
                    mrp_log_error("system-controller: refuse to set output ID "
                                  "%d (duplicate)", out->outputid);
                    out->outputid = old_id;
                }
            }
        }
    }

    if ((u->mask & MRP_WAYLAND_OUTPUT_NAME_MASK)) {
        if (u->outputname && strcmp(u->outputname, out->outputname)) {
            mask |= MRP_WAYLAND_OUTPUT_NAME_MASK;
            mrp_free(out->outputname);
            out->outputname = mrp_strdup(u->outputname);
        }
    }

    if (wl->output_update_callback) {
        wl->output_update_callback(wl, MRP_WAYLAND_OUTPUT_USER_REQUEST,
                                   mask, out);
    }
}

static bool output_constructor(mrp_wayland_t *wl, mrp_wayland_object_t *obj)
{
    static uint32_t index;
    static struct wl_output_listener listener =  {
        .geometry = geometry_callback,
        .mode     = mode_callback,
        .done     = done_callback,
        .scale    = scale_callback
    };

    char name[256];
    char buf[4096];

    mrp_wayland_output_t *out = (mrp_wayland_output_t *)obj;
    int sts;

    MRP_ASSERT(out, "invalid argument");

    snprintf(name, sizeof(name), "%s:%u", wl->display_name, index);

    out->index = index++;
    out->outputid = -1;
    out->outputname = mrp_strdup(name);

    if (!mrp_htbl_insert(wl->outputs.by_index, &out->index, out)) {
        mrp_log_error("failed to create output: already exists");
        mrp_free(out);
        return NULL;
    }

    sts = wl_output_add_listener((struct wl_output *)out->proxy,&listener,out);

    if (sts < 0)
        mrp_log_error("failed to add listener to output %d", out->index);
    else {
        if (wl->create_scripting_outputs) {
            out->scripting_data =
                mrp_wayland_scripting_output_create_from_c(NULL, out);
        }

        mrp_wayland_output_print(out, MRP_WAYLAND_OUTPUT_NAME_MASK,
                                 buf, sizeof(buf));
        mrp_debug("output %u created%s", out->index, buf);

        if (wl->output_update_callback) {
            wl->output_update_callback(wl, MRP_WAYLAND_OUTPUT_CREATE,
                                       MRP_WAYLAND_OUTPUT_NAME_MASK, out);
        }
    }

    return (sts < 0) ? false : true;
}

static void geometry_callback(void *data,
                              struct wl_output *wl_output,
                              int32_t pixel_x,
                              int32_t pixel_y,
                              int32_t physical_width,
                              int32_t physical_height,
                              int32_t subpixel,
                              const char *make,
                              const char *model,
                              int32_t transform)
{
#define UPDATE_INTEGER_FIELD(_f,_m)                       \
    if (out->_f != _f) {                                  \
        mask |= MRP_WAYLAND_OUTPUT_ ## _m ## _MASK;       \
        out->_f = _f;                                     \
    }
#define UPDATE_STRING_FIELD(_f,_m)                        \
    if (!out->_f || strcmp(out->_f, _f)) {                \
        mrp_free((void *)out->_f);                        \
        mask |= MRP_WAYLAND_OUTPUT_ ## _m ## _MASK;       \
            out->_f = mrp_strdup(_f ? _f : "<unknown>");  \
    }

    mrp_wayland_output_t *out = (mrp_wayland_output_t *)data;
    mrp_wayland_output_update_mask_t mask = 0;
    mrp_wayland_t *wl;
    uint32_t rotate;
    bool flip;
    int32_t width, height;
    char buf[4096];

    MRP_UNUSED(transform);

    MRP_ASSERT(out && out->interface && out->interface->wl,"invalid argument");
    MRP_ASSERT(wl_output == (struct wl_output *)out->proxy,
               "confused with data structures");

    wl = out->interface->wl;

    mrp_debug("x=%d y=%d width=%d height=%d subpixel=%d make='%s' model='%s' "
              "transorm=%d", pixel_x,pixel_y, physical_width,physical_height,
              subpixel, make ? make : "", model ? model : "");

    switch (transform) {
    default:
    case WL_OUTPUT_TRANSFORM_NORMAL:       rotate = 0;    flip = false;  break;
    case WL_OUTPUT_TRANSFORM_90:           rotate = 90;   flip = false;  break;
    case WL_OUTPUT_TRANSFORM_180:          rotate = 180;  flip = false;  break;
    case WL_OUTPUT_TRANSFORM_270:          rotate = 270;  flip = false;  break;
    case WL_OUTPUT_TRANSFORM_FLIPPED:      rotate = 0;    flip = true;   break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:   rotate = 90;   flip = true;   break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:  rotate = 180;  flip = true;   break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:  rotate = 270;  flip = true;   break;
    };

    UPDATE_INTEGER_FIELD (pixel_x, PIXEL_X);
    UPDATE_INTEGER_FIELD (pixel_y, PIXEL_Y);
    UPDATE_INTEGER_FIELD (physical_width, PHYSICAL_WIDTH);
    UPDATE_INTEGER_FIELD (physical_height, PHYSICAL_HEIGHT);
    UPDATE_INTEGER_FIELD (subpixel, SUBPIXEL);
    UPDATE_STRING_FIELD  (make, MAKE);
    UPDATE_STRING_FIELD  (model, MODEL);
    UPDATE_INTEGER_FIELD (rotate, ROTATE);
    UPDATE_INTEGER_FIELD (flip, FLIP);

    if (rotate == 90 || rotate == 270) {
        if (out->pixel_width > 0 && out->pixel_height > 0) {
            width = out->pixel_height;
            height = out->pixel_width;

            UPDATE_INTEGER_FIELD (width, WIDTH);
            UPDATE_INTEGER_FIELD (height, HEIGHT);
        }
    }

    mrp_wayland_output_print(out, mask, buf,sizeof(buf));
    mrp_debug("output %u configure%s", out->index, buf);

    if (wl->output_update_callback)
        wl->output_update_callback(wl, MRP_WAYLAND_OUTPUT_CONFIGURE, mask,out);

    check_if_ready(wl, out);

#undef UPDATE_STRING_FIELD
#undef UPDATE_INTEGER_FIELD
}

static void mode_callback(void *data,
                          struct wl_output *wl_output,
                          uint32_t flags,
                          int32_t pixel_width,
                          int32_t pixel_height,
                          int32_t refresh)
{
#define UPDATE_INTEGER_FIELD(_f,_m)                       \
    if (out->_f != _f) {                                  \
        mask |= MRP_WAYLAND_OUTPUT_ ## _m ## _MASK;       \
        out->_f = _f;                                     \
    }
#define UPDATE_STRING_FIELD(_f,_m)                        \
    if (!out->_f || strcmp(out->_f, _f)) {                \
        mrp_free((void *)out->_f);                        \
        mask |= MRP_WAYLAND_OUTPUT_ ## _m ## _MASK;       \
            out->_f = mrp_strdup(_f ? _f : "<unknown>");  \
    }

    mrp_wayland_output_t *out = (mrp_wayland_output_t *)data;
    mrp_wayland_output_update_mask_t mask = 0;
    int32_t width;
    int32_t height;
    mrp_wayland_t *wl;

    MRP_ASSERT(out && out->interface && out->interface->wl,"invalid argument");
    MRP_ASSERT(wl_output == (struct wl_output *)out->proxy,
               "confused with data structures");

    wl = out->interface->wl;

    mrp_debug("flags=0x%x width=%d height=%d refresh=%d",
              flags, pixel_width,pixel_height, refresh);

    if (out->rotate == 90 || out->rotate == 270) {
        width = pixel_height;
        height = pixel_width;
    }
    else {
        width = pixel_width;
        height = pixel_height;
    }

    if (!(flags & WL_OUTPUT_MODE_CURRENT)) {
        mrp_debug("ignoring mode %dx%d@%d on output %u",
                 width,height, refresh/1000, out->index);
        return;
    }

    UPDATE_INTEGER_FIELD (pixel_width, PIXEL_WIDTH);
    UPDATE_INTEGER_FIELD (pixel_height, PIXEL_HEIGHT);
    UPDATE_INTEGER_FIELD (width, WIDTH);
    UPDATE_INTEGER_FIELD (height, HEIGHT);
    UPDATE_INTEGER_FIELD (refresh, REFRESH);

    if (wl->output_update_callback)
        wl->output_update_callback(wl, MRP_WAYLAND_OUTPUT_CONFIGURE, mask,out);

    check_if_ready(wl, out);

#undef UPDATE_STRING_FIELD
#undef UPDATE_INTEGER_FIELD
}

static void done_callback(void *data, struct wl_output *wl_output)
{
    mrp_wayland_output_t *out = (mrp_wayland_output_t *)data;
    mrp_wayland_t *wl;

    MRP_ASSERT(out && out->interface && out->interface->wl,
               "invalid argument");
    MRP_ASSERT(wl_output == (struct wl_output *)out->proxy,
               "confused with data structures");

    wl = out->interface->wl;

    mrp_debug("output %u update is done", out->index);

    if (wl->output_update_callback)
        wl->output_update_callback(wl, MRP_WAYLAND_OUTPUT_DONE, -1, out);
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
              out->index);
}

static void check_if_ready(mrp_wayland_t *wl, mrp_wayland_output_t *out)
{
    output_iterator_helper_t helper;
    char buf[4096];

    MRP_ASSERT(wl && out, "invalid argument");

    if (!out->initialized && out->width > 0 && out->height > 0) {
        out->initialized = true;

        mrp_wayland_output_print(out, -1, buf,sizeof(buf));
        mrp_log_info("system-controller: found output %u%s", out->index, buf);

        if (wl->output_update_callback)
            wl->output_update_callback(wl, MRP_WAYLAND_OUTPUT_DONE, -1, out);

        /* list the areas that belong to this output */
        memset(&helper, 0, sizeof(helper));
        helper.output = out;

        mrp_htbl_foreach(wl->areas, print_area, &helper);

        if (helper.count == 0) {
            mrp_log_info("system-controller: output '%s' has no areas",
                         out->outputname);
        }
    }
}

static int print_area(void *key, void *object, void *ud)
{
    output_iterator_helper_t *helper = (output_iterator_helper_t *)ud;
    mrp_wayland_area_t *area = (mrp_wayland_area_t *)object;
    char buf[4096];

    MRP_UNUSED(key);

    MRP_ASSERT(helper && helper->output && area,
               "confused with data structures");

    if (helper->output == area->output) {
        helper->count++;

        mrp_wayland_area_print(area, -1, buf, sizeof(buf));
        mrp_log_info("system-controller: area for output '%s'%s",
                     helper->output->outputname, buf);
    }

    return MRP_HTBL_ITER_MORE;
}

size_t mrp_wayland_output_print(mrp_wayland_output_t *out,
                                mrp_wayland_output_update_mask_t mask,
                                char *buf,
                                size_t len)
{
#define PRINT(fmt, args...) \
    if (p < e) { p += snprintf(p, e-p, "\n      " fmt , ## args); }

    char *p, *e;

    e = (p = buf) + len;

    *p = 0;

    if ((mask & MRP_WAYLAND_OUTPUT_OUTPUTID_MASK))
        PRINT("id: %d", out->outputid);
    if ((mask & MRP_WAYLAND_OUTPUT_NAME_MASK))
        PRINT("outputname: '%s'", out->outputname ? out->outputname : "");
    if ((mask & MRP_WAYLAND_OUTPUT_PIXEL_POSITION_MASK))
        PRINT("pixel_position: %d,%d", out->pixel_x, out->pixel_y);
    if ((mask & MRP_WAYLAND_OUTPUT_PHYSICAL_SIZE_MASK))
        PRINT("physical_size: %dx%d mm", out->physical_width,
                                         out->physical_height);
    if ((mask & MRP_WAYLAND_OUTPUT_PIXEL_SIZE_MASK))
        PRINT("pixel_size: %dx%d pixel", out->pixel_width, out->pixel_height);
    if ((mask & MRP_WAYLAND_OUTPUT_SIZE_MASK))
        PRINT("size: %dx%d pixel", out->width, out->height);
    if ((mask & MRP_WAYLAND_OUTPUT_SUBPIXEL_MASK))
        PRINT("subpixel: %d", out->subpixel);
    if ((mask & MRP_WAYLAND_OUTPUT_ROTATE_MASK))
        PRINT("rotate: %u degrees", out->rotate);
    if ((mask & MRP_WAYLAND_OUTPUT_FLIP_MASK))
        PRINT("flip: %s", out->flip ? "yes" : "no");
    if ((mask & MRP_WAYLAND_OUTPUT_MONITOR_MASK)) {
        PRINT("monitor: %s manufactured by %s",
              out->model ? out->model: "<unknown>",
              out->make  ? out->make  : "<unknown>");
    }
    if ((mask & MRP_WAYLAND_OUTPUT_REFRESH_MASK))
        PRINT("refresh: %d Hz", out->refresh / 1000);

    return p - buf;

#undef PRINT
}


size_t mrp_wayland_output_request_print(mrp_wayland_output_update_t *u,
                                        char *buf, size_t len)
{
#define PRINT(fmt, args...) \
    if (p < e) { p += snprintf(p, e-p, "\n      " fmt , ## args); }

    mrp_wayland_output_update_mask_t mask;
    char *p, *e;

    mask = u->mask;
    e = (p = buf) + len;

    *p = 0;

    if ((mask & MRP_WAYLAND_OUTPUT_NAME_MASK))
        PRINT("outputname: '%s'", u->outputname);

    return p - buf;

#undef PRINT
}


const char *
mrp_wayland_output_update_mask_str(mrp_wayland_output_update_mask_t mask)
{
    switch (mask) {
    case MRP_WAYLAND_OUTPUT_INDEX_MASK:           return "index";
    case MRP_WAYLAND_OUTPUT_OUTPUTID_MASK:        return "id";
    case MRP_WAYLAND_OUTPUT_NAME_MASK:            return "name";
    case MRP_WAYLAND_OUTPUT_PIXEL_X_MASK:         return "pixel_x";
    case MRP_WAYLAND_OUTPUT_PIXEL_Y_MASK:         return "pixel_y";
    case MRP_WAYLAND_OUTPUT_PIXEL_POSITION_MASK:  return "pixel_position";
    case MRP_WAYLAND_OUTPUT_PHYSICAL_WIDTH_MASK:  return "physical_width";
    case MRP_WAYLAND_OUTPUT_PHYSICAL_HEIGHT_MASK: return "physical_height";
    case MRP_WAYLAND_OUTPUT_PHYSICAL_SIZE_MASK:   return "physical_size";
    case MRP_WAYLAND_OUTPUT_PIXEL_WIDTH_MASK:     return "pixel_width";
    case MRP_WAYLAND_OUTPUT_PIXEL_HEIGHT_MASK:    return "pixel_height";
    case MRP_WAYLAND_OUTPUT_PIXEL_SIZE_MASK:      return "pixel_size";
    case MRP_WAYLAND_OUTPUT_WIDTH_MASK:           return "width";
    case MRP_WAYLAND_OUTPUT_HEIGHT_MASK:          return "height";
    case MRP_WAYLAND_OUTPUT_SIZE_MASK:            return "size";
    case MRP_WAYLAND_OUTPUT_SUBPIXEL_MASK:        return "subpixel";
    case MRP_WAYLAND_OUTPUT_MAKE_MASK:            return "make";
    case MRP_WAYLAND_OUTPUT_MODEL_MASK:           return "model";
    case MRP_WAYLAND_OUTPUT_ROTATE_MASK:          return "rotate";
    case MRP_WAYLAND_OUTPUT_FLIP_MASK:            return "flip";
    case MRP_WAYLAND_OUTPUT_REFRESH_MASK:         return "refresh";
    default:                                      return "<unknown>";
    }
}

void mrp_wayland_output_set_scripting_data(mrp_wayland_output_t *out,
                                           void *data)
{
    MRP_ASSERT(out, "Invalid Argument");

    mrp_debug("%sset scripting data", data ? "" : "re");

    out->scripting_data = data;
}

