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

static mrp_wayland_layer_update_mask_t update(mrp_wayland_layer_t *,
                                              mrp_wayland_layer_update_t *);


mrp_wayland_layer_t *mrp_wayland_layer_create(mrp_wayland_t *wl,
                                              mrp_wayland_layer_update_t *u)
{
    mrp_wayland_layer_t *layer;
    mrp_wayland_layer_update_mask_t mask;
    char buf[2048];

    MRP_ASSERT(wl && u && (u->mask & MRP_WAYLAND_LAYER_LAYERID_MASK),
               "invalid argument");

    if (!wl->wm) {
        mrp_log_error("failed to create layer %d: no window manager",
                      u->layerid);
        return NULL;
    }

    if (!(layer = mrp_allocz(sizeof(mrp_wayland_layer_t)))) {
        mrp_log_error("failed to create layer %d: out of memory",
                      u->layerid);
        return NULL;
    }

    layer->wm = wl->wm;
    layer->layerid =  u->layerid;

    if (!mrp_htbl_insert(wl->layers, &layer->layerid, layer)) {
        mrp_log_error("failed to create layer: already exists");
        mrp_free(layer);
        return NULL;
    }

    if (!(u->mask & MRP_WAYLAND_LAYER_NAME_MASK)) {
        snprintf(buf, sizeof(buf), "layer-%d", layer->layerid);

        u->mask |= MRP_WAYLAND_LAYER_NAME_MASK;
        u->name = buf;
    }

    mask = update(layer, u);

    mrp_wayland_layer_print(layer, mask, buf,sizeof(buf));
    mrp_debug("layer %d created%s", layer->layerid, buf);

    return layer;
}

void mrp_wayland_layer_destroy(mrp_wayland_layer_t *layer)
{
    mrp_wayland_window_manager_t *wm;
    mrp_wayland_interface_t *interface;
    mrp_wayland_t *wl;
    char buf[1024];

    MRP_ASSERT(layer && layer->wm && layer->wm->interface &&
               layer->wm->interface->wl, "invalid argument");

    wm = layer->wm;
    interface = wm->interface;
    wl = interface->wl;

    mrp_wayland_layer_print(layer, MRP_WAYLAND_LAYER_NAME_MASK,
                            buf, sizeof(buf));
    mrp_debug("destroying layer %d%s", layer->layerid, buf);

    mrp_free(layer->name);

    if ((void *)layer != mrp_htbl_remove(wl->layers, &layer->layerid, false)) {
        mrp_log_error("failed to destroy layer %d: confused with "
                      "data structures", layer->layerid);
        return;
    }

    free(layer);
}

mrp_wayland_layer_t *mrp_wayland_layer_find(mrp_wayland_t *wl, int32_t layerid)
{
    MRP_ASSERT(wl, "invalid argument");

    return (mrp_wayland_layer_t*)mrp_htbl_lookup(wl->layers,&layerid);
}

void mrp_wayland_layer_visibility_request(mrp_wayland_layer_t *layer,
                                          int32_t visible)
{
    mrp_wayland_window_manager_t *wm;
    mrp_wayland_layer_update_t u;

    MRP_ASSERT(layer && layer->wm, "invalid arguent");

    wm = layer->wm;

    memset(&u, 0, sizeof(u));

    if (visible >= 0) {
        u.mask |= MRP_WAYLAND_LAYER_VISIBLE_MASK;
        u.visible = visible ? true : false;
    }

    wm->layer_request(layer, &u);
}

void mrp_wayland_layer_update(mrp_wayland_layer_t *layer,
                              mrp_wayland_layer_update_t *u)
{
    int32_t layerid;
    mrp_wayland_layer_update_mask_t mask;
    char buf[2048];

    MRP_ASSERT(layer && layer->wm && layer->wm->interface &&
               layer->wm->interface->wl && u, "invalid argument");

    layerid = layer->layerid;

    if ((u->mask & MRP_WAYLAND_LAYER_LAYERID_MASK)) {
        if (u->layerid != layerid) {
            mrp_log_error("attempt to change layerid to %d of "
                          "existing layer %d", u->layerid, layerid);
            return;
        }
    }

    mask = update(layer, u);

    if (!mask)
        mrp_debug("layer %d update requested but nothing changed", layerid);
    else {
        mrp_wayland_layer_print(layer, mask, buf,sizeof(buf));
        mrp_debug("layer %d updated%s", layerid, buf);
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
    if ((mask & MRP_WAYLAND_LAYER_VISIBLE_MASK))
        PRINT("visible: %s", layer->visible ? "yes" : "no");

    return p - buf;

#undef PRINT
}

size_t mrp_wayland_layer_request_print(mrp_wayland_layer_update_t *u,
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
        PRINT("name: '%s'", u->name);
    if ((mask & MRP_WAYLAND_LAYER_VISIBLE_MASK))
        PRINT("visible: %s", u->visible ? "yes" : "no");

    return p - buf;

#undef PRINT
}

static mrp_wayland_layer_update_mask_t update(mrp_wayland_layer_t *layer,
                                              mrp_wayland_layer_update_t *u)
{
    mrp_wayland_layer_update_mask_t mask = 0;

    if ((u->mask & MRP_WAYLAND_LAYER_NAME_MASK)) {
        if (!layer->name || strcmp(u->name, layer->name)) {
            mask |= MRP_WAYLAND_LAYER_NAME_MASK;
            mrp_free(layer->name);
            layer->name = mrp_strdup(u->name);
        }
    }

    if ((u->mask & MRP_WAYLAND_LAYER_VISIBLE_MASK)) {
        if ((u->visible && !layer->visible)||(!u->visible && layer->visible)) {
            mask |= MRP_WAYLAND_LAYER_VISIBLE_MASK;
            layer->visible = u->visible;
        }
    }

    return mask;
}
