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

#include "animation.h"

static const char *type_str(mrp_wayland_animation_type_t type);

mrp_wayland_animation_t *mrp_wayland_animation_create(void)
{
    size_t size;
    mrp_wayland_animation_t *anims;

    size = sizeof(mrp_wayland_animation_t) * MRP_WAYLAND_ANIMATION_MAX;

    if (!(anims = mrp_allocz(size)))
        mrp_log_error("can't get memory for animation");

    return anims;
}

void mrp_wayland_animation_destroy(mrp_wayland_animation_t *anims)
{
    int i;

    if (anims) {
        for (i = 0;  i < MRP_WAYLAND_ANIMATION_MAX;  i++)
            mrp_free((void *)anims[i].name);
        mrp_free(anims);
    }
}

bool mrp_wayland_animation_set(mrp_wayland_animation_t *anims,
                               mrp_wayland_animation_type_t type,
                               const char *name,
                               int32_t time)
{
    mrp_wayland_animation_t *anim;

    MRP_ASSERT(anims, "invalid argument");

    if (type < 0 || type > MRP_WAYLAND_ANIMATION_MAX)
        return false;

    anim = anims + type;

    mrp_free((void *)anim->name);

    if (!name || !name[0] || time <= 0)
        memset(anim, 0, sizeof(mrp_wayland_animation_t));
    else {
        anim->type = type;
        anim->name = mrp_strdup(name);
        anim->time = time;
    }

    return true;
}

size_t mrp_wayland_animation_print(mrp_wayland_animation_t *anims,
                                   char *buf, size_t len)
{
#define PRINT(fmt, args...) \
    if (p < e) { p += snprintf(p, e-p, "\n      " fmt , ## args); }

    mrp_wayland_animation_t *anim;
    char *p, *e;
    size_t i;
    bool empty;

    MRP_ASSERT(buf && len > 0, "invalid argument");

    e = (p = buf) + len;
    empty = true;

    if (anims) {
        for (i = 0;   i < MRP_WAYLAND_ANIMATION_MAX;   i++) {
            anim = anims + i;

            if (anim->name && anim->name[0]) {
                PRINT("%s: '%s' %d", type_str(i), anim->name, anim->time);
                empty = false;
            }
        }
    }

    if (empty)
        PRINT("<none>");

    return p - buf;

#undef PRINT
}

static const char *type_str(mrp_wayland_animation_type_t type)
{
    const char *str;

    switch (type) {
    case MRP_WAYLAND_ANIMATION_HIDE:     str = "hide";              break;
    case MRP_WAYLAND_ANIMATION_SHOW:     str = "show";              break;
    case MRP_WAYLAND_ANIMATION_MOVE:     str = "move";              break;
    case MRP_WAYLAND_ANIMATION_RESIZE:   str = "resize";            break;
    case MRP_WAYLAND_ANIMATION_MAP:      str = "map";               break;
    default:                             str = "<not-supported>";   break;
    }

    return str;
}
