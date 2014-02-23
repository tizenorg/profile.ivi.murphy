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

#ifndef __MURPHY_SYSTEM_CONTROLLER_SCREEN_H__
#define __MURPHY_SYSTEM_CONTROLLER_SCREEN_H__

#include <sys/types.h>

#include <murphy/common/hashtbl.h>

#include "resource-manager.h"
#include "wayland/wayland.h"

struct mrp_resmgr_screen_area_s {
    mrp_list_hook_t link;       /* to maintain the list of areas in a zone */
    const char *name;
    int32_t outputid;
    int32_t x, y;
    int32_t width, height;
    mrp_list_hook_t resources;
    size_t noverlap;
    size_t *overlaps;
    int32_t zorder;
};

struct mrp_resmgr_screen_s {
    mrp_resmgr_t *resmgr;
    uint32_t resid;
    mrp_htbl_t *resources;               /* to access resources by surfaceid */
    mrp_list_hook_t zones[MRP_ZONE_MAX]; /* list of areas in the zones */
    uint32_t grantids[MRP_ZONE_MAX];
    size_t narea;
    mrp_resmgr_screen_area_t **areas;    /* areas to access by index */
};


mrp_resmgr_screen_t *mrp_resmgr_screen_create(mrp_resmgr_t *resmgr);
void mrp_resmgr_screen_destroy(mrp_resmgr_screen_t *screen);

int mrp_resmgr_screen_disable(mrp_resmgr_screen_t *screen,
                              const char *output_name,
                              const char *area_name,
                              bool disable,
                              mrp_resmgr_disable_t type,
                              void *data,
                              bool recalc_owner);

int mrp_resmgr_screen_print(mrp_resmgr_screen_t *screen, uint32_t areaid,
                            char *buf, int len);

void mrp_resmgr_screen_area_create(mrp_resmgr_screen_t *screen,
                                   mrp_wayland_area_t *wlarea,
                                   const char *zonename);
void mrp_screen_area_destroy(mrp_resmgr_screen_t *screen,
                             int32_t areaid);

void mrp_screen_resource_raise(mrp_resmgr_screen_t *screen,
                               const char *appid,
                               int32_t surfaceid);
void mrp_screen_resource_lower(mrp_resmgr_screen_t *screen,
                               const char *appid,
                               int32_t surfaceid);


#endif /* __MURPHY_SYSTEM_CONTROLLER_SCREEN_H__ */
