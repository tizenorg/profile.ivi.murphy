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

#ifndef __MURPHY_WAYLAND_ANIMATION_H__
#define __MURPHY_WAYLAND_ANIMATION_H__

#include <sys/types.h>

#include "wayland/wayland.h"


enum mrp_wayland_animation_type_e {
    MRP_WAYLAND_ANIMATION_HIDE = 0,
    MRP_WAYLAND_ANIMATION_SHOW,
    MRP_WAYLAND_ANIMATION_MOVE,
    MRP_WAYLAND_ANIMATION_RESIZE,

    MRP_WAYLAND_ANIMATION_MAX
};

struct mrp_wayland_animation_s {
    mrp_wayland_animation_type_t type;
    const char *name;
    int32_t time;
};

mrp_wayland_animation_t *mrp_wayland_animation_create(void);
void mrp_wayland_animation_destroy(mrp_wayland_animation_t *);

bool mrp_wayland_animation_set(mrp_wayland_animation_t *anims,
                               mrp_wayland_animation_type_t type,
                               const char *name,
                               int32_t time);

size_t mrp_wayland_animation_print(mrp_wayland_animation_t *anims,
                                   char *buf, size_t len);


#endif /* __MURPHY_WAYLAND_ANIMATION_H__ */
