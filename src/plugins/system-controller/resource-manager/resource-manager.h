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

#ifndef __MURPHY_SYSTEM_CONTROLLER_RESOURCE_MANAGER_H__
#define __MURPHY_SYSTEM_CONTROLLER_RESOURCE_MANAGER_H__

#include <sys/types.h>

#include <murphy/common/hashtbl.h>

#include <murphy/resource/data-types.h>

#include "data-types.h"

typedef enum mrp_sysctl_scripting_field_e  mrp_resmgr_scripting_field_t;

typedef struct mrp_resmgr_s                mrp_resmgr_t;
typedef struct mrp_resmgr_screen_s         mrp_resmgr_screen_t;
typedef struct mrp_resmgr_audio_s          mrp_resmgr_audio_t;
typedef struct mrp_resmgr_input_s          mrp_resmgr_input_t;

#define MRP_RESMGR_RESOURCE_MAX            256
#define MRP_RESMGR_RESOURCE_BUCKETS        (MRP_RESMGR_RESOURCE_MAX / 4)

struct mrp_resmgr_s {
    mrp_htbl_t *resources;
    mrp_zone_mask_t zones;

    mrp_resmgr_screen_t *screen;
    mrp_resmgr_audio_t *audio;
    mrp_resmgr_input_t *input;

    void *scripting_data;
};


mrp_resmgr_t *mrp_resmgr_create(void);
void mrp_resmgr_destroy(mrp_resmgr_t *resmgr);

void  mrp_resmgr_insert_resource(mrp_resmgr_t *resmgr, mrp_zone_t *zone,
                                 mrp_resource_t *key, void *resource);
void *mrp_resmgr_remove_resource(mrp_resmgr_t *resmgr, mrp_zone_t *zone,
                                 mrp_resource_t *key);
void *mrp_resmgr_lookup_resource(mrp_resmgr_t *resmgr, mrp_resource_t *key);


#endif /* __MURPHY_SYSTEM_CONTROLLER_RESOURCE_MANAGER_H__ */
