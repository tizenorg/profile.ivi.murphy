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

#ifndef __MURPHY_SYSTEM_CONTROLLER_RESOURCE_CLIENT_H__
#define __MURPHY_SYSTEM_CONTROLLER_RESOURCE_CLIENT_H__

#include <sys/types.h>

#include <murphy/common/hashtbl.h>

#include <murphy/resource/data-types.h>

#include "data-types.h"

#define MRP_RESCLNT_RESOURCE_SET_MAX      256

#define MRP_RESCLNT_RESOURCE_SET_BUCKETS  (MRP_RESCLNT_RESOURCE_SET_MAX / 8)

typedef enum mrp_sysctl_scripting_field_e      mrp_resclnt_scripting_field_t;
typedef enum mrp_resclnt_resource_set_type_e   mrp_resclnt_resource_set_type_t;

typedef struct mrp_resclnt_s                   mrp_resclnt_t;


enum mrp_resclnt_resource_set_type_e {
    MRP_RESCLIENT_RESOURCE_SET_UNKNOWN = 0,
    MRP_RESCLIENT_RESOURCE_SET_SCREEN,
    MRP_RESCLIENT_RESOURCE_SET_AUDIO,
    MRP_RESCLIENT_RESOURCE_SET_INPUT,

    MRP_RESCLIENT_RESOURCE_SET_MAX
};


struct mrp_resclnt_s {
    mrp_resource_client_t *client;
    uint32_t reqno;

    struct {
        mrp_htbl_t *screen;
        mrp_htbl_t *audio;
        mrp_htbl_t *input;
    } rsets;

    void *scripting_data;
};


mrp_resclnt_t *mrp_resclnt_create(void);
void mrp_resclnt_destroy(mrp_resclnt_t *resclnt);

bool mrp_resclnt_add_resource_set(mrp_resclnt_t *resclnt,
                                  mrp_resclnt_resource_set_type_t type,
                                  const char *zone_name,
                                  const char *appid,
                                  void *key);
void mrp_resclnt_remove_resource_set(mrp_resclnt_t *resclnt,
                                     mrp_resclnt_resource_set_type_t type,
                                     void *key);

bool mrp_resclnt_acquire_resource_set(mrp_resclnt_t *resclnt,
                                      mrp_resclnt_resource_set_type_t type,
                                      void *key);
bool mrp_resclnt_release_resource_set(mrp_resclnt_t *resclnt,
                                      mrp_resclnt_resource_set_type_t type,
                                      void *key);


#endif /* __MURPHY_SYSTEM_CONTROLLER_RESOURCE_CLIENT_H__ */
