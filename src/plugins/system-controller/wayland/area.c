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

#include "area.h"
#include "window-manager.h"
#include "output.h"
#include "scripting-wayland.h"


static char *align_str(mrp_wayland_area_align_t, char *, size_t);

static int set_area_for_applications(void *, void *, void *);



mrp_wayland_area_t *mrp_wayland_area_create(mrp_wayland_t *wl,
                                            mrp_wayland_area_update_t *u)
{
#define IF_PRESENT(u,n) (u->mask & MRP_WAYLAND_AREA_ ## n ## _MASK)

    static mrp_wayland_area_align_t left_and_right =
        MRP_WAYLAND_AREA_ALIGN_LEFT  |
        MRP_WAYLAND_AREA_ALIGN_RIGHT ;
    static mrp_wayland_area_align_t top_and_bottom =
        MRP_WAYLAND_AREA_ALIGN_LEFT  |
        MRP_WAYLAND_AREA_ALIGN_RIGHT ;


    mrp_wayland_area_update_mask_t mask;
    mrp_wayland_area_t *area;
    char fullname[2048];
    char buf[2048];

    MRP_ASSERT(wl && u &&
               (u->mask & MRP_WAYLAND_AREA_NAME_MASK) && u->name &&
               (u->mask & MRP_WAYLAND_AREA_OUTPUT_MASK) &&
               (u->mask & MRP_WAYLAND_AREA_WIDTH_MASK) &&
               (u->mask & MRP_WAYLAND_AREA_HEIGHT_MASK),
               "invalid argument");

    if (!(area = mrp_allocz(sizeof(mrp_wayland_area_t)))) {
        mrp_log_error("failed to create area %d: out of memory",
                      u->areaid);
        return NULL;
    }

    snprintf(fullname, sizeof(fullname), "%s.%s",
             u->output->outputname, u->name);

    area->wl = wl;
    area->wm = wl->wm;
    area->name = mrp_strdup(u->name);
    area->fullname = mrp_strdup(fullname);
    area->output = u->output;
    area->width = u->width;
    area->height = u->height;

    mask = u->mask | MRP_WAYLAND_AREA_FULLNAME_MASK;

    area->areaid    = IF_PRESENT(u, AREAID   ) ? u->areaid : -1;
    area->x         = IF_PRESENT(u, X        ) ? u->x : 0;
    area->y         = IF_PRESENT(u, Y        ) ? u->y : 0;
    area->keepratio = IF_PRESENT(u, KEEPRATIO) ? u->keepratio : false;
    area->align     = IF_PRESENT(u, ALIGN    ) ? u->align : 0;

    if ((area->align & MRP_WAYLAND_AREA_ALIGN_HMASK) == left_and_right)
        area->align &= ~MRP_WAYLAND_AREA_ALIGN_HMASK; /* align middle */

    if ((area->align & MRP_WAYLAND_AREA_ALIGN_VMASK) == top_and_bottom)
        area->align &= ~MRP_WAYLAND_AREA_ALIGN_VMASK; /* align middle */

    if (!mrp_htbl_insert(wl->areas, area->fullname, area)) {
        mrp_log_error("failed to create area: already exists");
        mrp_free(area->name);
        mrp_free(area->fullname);
        mrp_free(area);
        return NULL;
    }

    if (wl->create_scripting_areas) {
        area->scripting_data = mrp_wayland_scripting_area_create_from_c(NULL,
                                                                        area);
    }

    mrp_wayland_area_print(area, mask, buf,sizeof(buf));
    mrp_debug("area '%s' created%s", area->name, buf);

    if (wl->area_update_callback)
        wl->area_update_callback(wl, MRP_WAYLAND_AREA_CREATE, mask, area);

    mrp_application_foreach(set_area_for_applications, area);

    return area;

#undef IF_PRESENT
}

void mrp_wayland_area_destroy(mrp_wayland_area_t *area)
{
    mrp_wayland_t *wl;
    char buf[1024];

    MRP_ASSERT(area && area->wl, "invalid argument");

    wl = area->wl;

    mrp_wayland_area_print(area, MRP_WAYLAND_AREA_AREAID_MASK,
                           buf, sizeof(buf));
    mrp_debug("destroying area '%s'%s", area->name, buf);

    if (wl->area_update_callback)
        wl->area_update_callback(wl, MRP_WAYLAND_AREA_DESTROY, 0, area);

    if ((void *)area != mrp_htbl_remove(wl->areas, area->name, false)) {
        mrp_log_error("failed to destroy area '%s': confused with "
                      "data structures", area->name);
        return;
    }

    mrp_free(area->name);

    free(area);
}


mrp_wayland_area_t *mrp_wayland_area_find(mrp_wayland_t *wl,
                                          const char *fullname)
{
    MRP_ASSERT(wl, "invalid argument");

    return (mrp_wayland_area_t*)mrp_htbl_lookup(wl->areas, (void *)fullname);
}


size_t mrp_wayland_area_print(mrp_wayland_area_t *area,
                               mrp_wayland_area_update_mask_t mask,
                               char *buf, size_t len)
{
#define PRINT(fmt, args...) \
    if (p < e) { p += snprintf(p, e-p, "\n      " fmt , ## args); }

    mrp_wayland_output_t *out;
    char *p, *e;
    char as[256];

    out = area->output;
    e = (p = buf) + len;

    *p = 0;

    if ((mask & MRP_WAYLAND_AREA_AREAID_MASK))
        PRINT("areaid: %d", area->areaid);
    if ((mask & MRP_WAYLAND_AREA_NAME_MASK))
        PRINT("name: '%s'", area->name);
    if ((mask & MRP_WAYLAND_AREA_FULLNAME_MASK))
        PRINT("fullname: '%s'", area->fullname);
    if ((mask & MRP_WAYLAND_AREA_OUTPUT_MASK))
        PRINT("output: '%s'", out ? out->outputname : "<not set>");
    if ((mask & MRP_WAYLAND_AREA_POSITION_MASK))
        PRINT("position: %d,%d", area->x, area->y);
    if ((mask & MRP_WAYLAND_AREA_SIZE_MASK))
        PRINT("size: %dx%d", area->width, area->height);
    if ((mask & MRP_WAYLAND_AREA_KEEPRATIO_MASK))
        PRINT("keepratio: %s", area->keepratio ? "yes" : "no");
    if ((mask & MRP_WAYLAND_AREA_ALIGN_MASK))
        PRINT("align: 0x%x =%s", area->align, align_str(area->align,
                                                        as, sizeof(as)));
    return p - buf;

#undef PRINT
}

const char *mrp_wayland_area_align_str(mrp_wayland_area_align_t align)
{
    switch (align) {
    case MRP_WAYLAND_AREA_ALIGN_LEFT:    return "left";
    case MRP_WAYLAND_AREA_ALIGN_RIGHT:   return "right";
    case MRP_WAYLAND_AREA_ALIGN_TOP:     return "top";
    case MRP_WAYLAND_AREA_ALIGN_BOTTOM:  return "bottom";
    default:                             return "middle";
    }
}

void mrp_wayland_area_set_scripting_data(mrp_wayland_area_t *area, void *data)
{
    MRP_ASSERT(area, "Invalid Argument");

    mrp_debug("%sset scripting data", data ? "" : "re");

    area->scripting_data = data;
}


static char *align_str(mrp_wayland_area_align_t align, char *buf, size_t len)
{
#define PRINT(fmt, args... ) \
    if (p < e) { p += snprintf(p, e-p, " " fmt , ## args); }

    typedef struct {
        mrp_wayland_area_align_t mask;
        const char *name;
    } map_t;

    static map_t hmap[] = {
        { MRP_WAYLAND_AREA_ALIGN_LEFT   ,  "left"    },
        { MRP_WAYLAND_AREA_ALIGN_MIDDLE ,  "hmiddle" },
        { MRP_WAYLAND_AREA_ALIGN_RIGHT  ,  "right"   },
        {                 0,                NULL     }
    };
    static map_t vmap[] = {
        { MRP_WAYLAND_AREA_ALIGN_TOP    ,  "top"     },
        { MRP_WAYLAND_AREA_ALIGN_MIDDLE ,  "vmiddle" },
        { MRP_WAYLAND_AREA_ALIGN_BOTTOM ,  "bottom"  },
        {                 0,                NULL     }
    };

    mrp_wayland_area_align_t halign, valign;
    map_t *m;
    char *p, *e;

    halign = align & MRP_WAYLAND_AREA_ALIGN_HMASK;
    valign = align & MRP_WAYLAND_AREA_ALIGN_VMASK;

    e = (p = buf) + len;

    *p = 0;

    for (m = hmap;   m->name;   m++) {
        if ((halign == m->mask)) {
                PRINT("%s", m->name);
                align &= ~m->mask;
                break;
        }
    }

    for (m = vmap;   m->name;   m++) {
        if ((valign == m->mask)) {
                PRINT("%s", m->name);
                align &= ~m->mask;
                break;
        }
    }

    if (align)
        PRINT("<unknown 0x%x>", align);

    return buf;

#undef PRINT
}


static int set_area_for_applications(void *key, void *object, void *ud)
{
    mrp_application_t *app = (mrp_application_t *)object;
    mrp_wayland_area_t *area = (mrp_wayland_area_t *)ud;

    MRP_UNUSED(key);

    if (!strcmp(app->area_name, area->fullname)) {
        mrp_debug("set area '%s' for app '%s'", area->name, app->appid);
        app->area = area;
    }

    return MRP_HTBL_ITER_MORE;
}
