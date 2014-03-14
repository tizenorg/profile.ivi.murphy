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

#include "layer.h"
#include "window-manager.h"
#include "scripting-wayland.h"

static mrp_wayland_layer_update_mask_t update(mrp_wayland_layer_t *,
                                              mrp_wayland_layer_update_t *);


mrp_wayland_layer_t *mrp_wayland_layer_create(mrp_wayland_t *wl,
                                              mrp_wayland_layer_update_t *u)
{
    mrp_wayland_layer_t *layer;
    mrp_wayland_layer_update_mask_t mask;
    char buf[2048];

    MRP_ASSERT(wl && u && (u->mask & MRP_WAYLAND_LAYER_LAYERID_MASK) &&
               (u->mask & MRP_WAYLAND_LAYER_TYPE_MASK), "invalid argument");

    if (!(layer = mrp_allocz(sizeof(mrp_wayland_layer_t)))) {
        mrp_log_error("system-controller: failed to create layer %d: "
                      "out of memory", u->layerid);
        return NULL;
    }

    layer->wl = wl;
    layer->wm = wl->wm;
    layer->layerid = u->layerid;
    layer->type = u->type;

    if (!mrp_htbl_insert(wl->layers.by_id, &layer->layerid, layer)) {
        mrp_log_error("system-controller: failed to create layer: "
                      "already exists");
        mrp_free(layer);
        return NULL;
    }

    if (!mrp_htbl_insert(wl->layers.by_type, &layer->type, layer)) {
        mrp_log_warning("system-controller: layer id %d will "
                        "not be mapped to layer type", layer->layerid);
    }

    if (!(u->mask & MRP_WAYLAND_LAYER_NAME_MASK)) {
        snprintf(buf, sizeof(buf), "layer-%d", layer->layerid);

        u->mask |= MRP_WAYLAND_LAYER_NAME_MASK;
        u->name = buf;
    }

    mask  = MRP_WAYLAND_LAYER_LAYERID_MASK | MRP_WAYLAND_LAYER_TYPE_MASK;
    mask |= update(layer, u);

    if (wl->create_scripting_layers)
        layer->scripting_data = mrp_wayland_scripting_layer_create_from_c(NULL,
                                                                        layer);
    mrp_wayland_layer_print(layer, mask, buf,sizeof(buf));
    mrp_debug("layer %d created%s", layer->layerid, buf);

    if (wl->layer_update_callback)
        wl->layer_update_callback(wl, MRP_WAYLAND_LAYER_CREATE, mask, layer);

    return layer;
}

void mrp_wayland_layer_destroy(mrp_wayland_layer_t *layer)
{
    mrp_wayland_t *wl;
    char buf[1024];
    void *vl;

    MRP_ASSERT(layer && layer->wl, "invalid argument");

    wl = layer->wl;

    mrp_wayland_layer_print(layer, MRP_WAYLAND_LAYER_NAME_MASK,
                            buf, sizeof(buf));
    mrp_debug("destroying layer %d%s", layer->layerid, buf);

    if (wl->layer_update_callback)
        wl->layer_update_callback(wl, MRP_WAYLAND_LAYER_DESTROY, 0, layer);

    mrp_free(layer->name);
    vl = (void *)layer;

    if (vl != mrp_htbl_remove(wl->layers.by_id, &layer->layerid, false) ||
        vl != mrp_htbl_remove(wl->layers.by_type, &layer->type, false))
    {
        mrp_log_error("system-controller: failed to destroy layer %d: "
                      "confused with data structures", layer->layerid);
        return;
    }

    free(layer);
}

mrp_wayland_layer_t *mrp_wayland_layer_find_by_id(mrp_wayland_t *wl,
                                                  int32_t layerid)
{
    MRP_ASSERT(wl, "invalid argument");

    return (mrp_wayland_layer_t*)mrp_htbl_lookup(wl->layers.by_id, &layerid);
}

mrp_wayland_layer_t *mrp_wayland_layer_find_by_type(mrp_wayland_t *wl,
                                                 mrp_wayland_layer_type_t type)
{
    MRP_ASSERT(wl, "invalid argument");

    return (mrp_wayland_layer_t*)mrp_htbl_lookup(wl->layers.by_type, &type);
}

void mrp_wayland_layer_request(mrp_wayland_t *wl,mrp_wayland_layer_update_t *u)
{
    mrp_wayland_layer_t *layer;
    mrp_wayland_window_manager_t *wm;

    MRP_ASSERT(wl && u, "invalid arguments");

    if (!(u->mask & MRP_WAYLAND_LAYER_LAYERID_MASK) ||
        !(layer = mrp_wayland_layer_find_by_id(wl, u->layerid)))
    {
        mrp_debug("can't find layer %u: request rejected", u->layerid);
        return;
    }

    MRP_ASSERT(wl == layer->wl, "confused with data structures");

    if ((wm = layer->wm))
        wm->layer_request(layer, u);
}

void mrp_wayland_layer_update(mrp_wayland_layer_t *layer,
                              mrp_wayland_layer_operation_t oper,
                              mrp_wayland_layer_update_t *u)
{
    mrp_wayland_t *wl;
    int32_t layerid;
    mrp_wayland_layer_update_mask_t mask;
    char buf[2048];

    MRP_ASSERT(layer && layer->wl && u, "invalid argument");

    wl = layer->wl;

    layerid = layer->layerid;

    if ((u->mask & MRP_WAYLAND_LAYER_LAYERID_MASK)) {
        if (u->layerid != layerid) {
            mrp_log_error("system-controller: attempt to change layerid "
                          "to %d of existing layer %d", u->layerid, layerid);
            return;
        }
    }

    mask = update(layer, u);

    if (!mask)
        mrp_debug("layer %d update requested but nothing changed", layerid);
    else {
        mrp_wayland_layer_print(layer, mask, buf,sizeof(buf));
        mrp_debug("layer %d updated%s", layerid, buf);

        if (wl->layer_update_callback)
            wl->layer_update_callback(wl, oper, mask, layer);
    }
}

size_t mrp_wayland_layer_print(mrp_wayland_layer_t *layer,
                               mrp_wayland_layer_update_mask_t mask,
                               char *buf,
                               size_t len)
{
#define PRINT(fmt, args...) \
    if (p < e) { p += snprintf(p, e-p, "\n      " fmt , ## args); }

    char *p, *e;

    e = (p = buf) + len;

    *p = 0;

    if ((mask & MRP_WAYLAND_LAYER_NAME_MASK))
        PRINT("name: '%s'", layer->name);
    if ((mask & MRP_WAYLAND_LAYER_TYPE_MASK))
        PRINT("type: %s", mrp_wayland_layer_type_str(layer->type));
    if ((mask & MRP_WAYLAND_LAYER_VISIBLE_MASK))
        PRINT("visible: %s", layer->visible ? "yes" : "no");

    return p - buf;

#undef PRINT
}

size_t mrp_wayland_layer_request_print(mrp_wayland_layer_update_t *u,
                                       char *buf, size_t len)
{
#define PRINT(fmt, args...) \
    if (p < e) { p += snprintf(p, e-p, "\n      " fmt , ## args); }

    mrp_wayland_layer_update_mask_t mask;
    char *p, *e;

    mask = u->mask;
    e = (p = buf) + len;

    *p = 0;

    if ((mask & MRP_WAYLAND_LAYER_NAME_MASK))
        PRINT("name: '%s'", u->name);
#if 0
    if ((mask & MRP_WAYLAND_LAYER_TYPE_MASK))
        PRINT("type: %s", mrp_wayland_layer_type_str(u->type));
#endif
    if ((mask & MRP_WAYLAND_LAYER_VISIBLE_MASK))
        PRINT("visible: %s", u->visible ? "yes" : "no");

    return p - buf;

#undef PRINT
}

const char *
mrp_wayland_layer_update_mask_str(mrp_wayland_layer_update_mask_t mask)
{
    switch (mask) {
    case MRP_WAYLAND_LAYER_LAYERID_MASK:   return "layerid";
    case MRP_WAYLAND_LAYER_NAME_MASK:      return "name";
    case MRP_WAYLAND_LAYER_TYPE_MASK:      return "type";
    case MRP_WAYLAND_LAYER_VISIBLE_MASK:   return "visible";
    default:                               return "<unknown>";
    }
}

const char *mrp_wayland_layer_type_str(mrp_wayland_layer_type_t type)
{
    switch (type) {
    case MRP_WAYLAND_LAYER_BACKGROUND:   return "background";
    case MRP_WAYLAND_LAYER_APPLICATION:  return "application";
    case MRP_WAYLAND_LAYER_INPUT:        return "input";
    case MRP_WAYLAND_LAYER_TOUCH:        return "touch";
    case MRP_WAYLAND_LAYER_CURSOR:       return "cursor";
    case MRP_WAYLAND_LAYER_STARTUP:      return "startup";
    case MRP_WAYLAND_LAYER_FULLSCREEN:   return "fullscreen";
    default:                             return "<unknown>";
    }
}

void mrp_wayland_layer_set_scripting_data(mrp_wayland_layer_t *layer,
                                          void *data)
{
    MRP_ASSERT(layer, "Invalid Argument");

    mrp_debug("%sset scripting data", data ? "" : "re");

    layer->scripting_data = data;
}


static mrp_wayland_layer_update_mask_t update(mrp_wayland_layer_t *layer,
                                              mrp_wayland_layer_update_t *u)
{
    mrp_wayland_layer_update_mask_t mask = 0;
    mrp_wayland_layer_update_mask_t passthrough = 0;
    mrp_wayland_window_manager_t *wm;

    if ((wm = layer->wm))
        passthrough = wm->passthrough.layer_update;

    if ((u->mask & MRP_WAYLAND_LAYER_NAME_MASK)) {
        if (!layer->name || strcmp(u->name, layer->name) ||
            (passthrough & MRP_WAYLAND_LAYER_NAME_MASK))
        {
            mask |= MRP_WAYLAND_LAYER_NAME_MASK;
            mrp_free(layer->name);
            layer->name = mrp_strdup(u->name);
        }
    }

#if 0
    if ((u->mask & MRP_WAYLAND_LAYER_TYPE_MASK)) {
        if (u->type != layer->type ||
            (passthrough & MRP_WAYLAND_LAYER_TYPE_MASK))
        {
            mask |= MRP_WAYLAND_LAYER_TYPE_MASK;
            layer->type = u->type;
        }
    }
#endif

    if ((u->mask & MRP_WAYLAND_LAYER_VISIBLE_MASK)) {
        if (( u->visible && !layer->visible) ||
            (!u->visible &&  layer->visible) ||
            (passthrough & MRP_WAYLAND_LAYER_VISIBLE_MASK))
        {
            mask |= MRP_WAYLAND_LAYER_VISIBLE_MASK;
            layer->visible = u->visible;
        }
    }

    return mask;
}
