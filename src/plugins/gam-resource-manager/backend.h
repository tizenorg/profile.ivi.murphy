/*
 * Copyright (c) 2014, Intel Corporation
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

#ifndef __MURPHY_GAM_RESOURCE_MANAGER_BACKEND_H__
#define __MURPHY_GAM_RESOURCE_MANAGER_BACKEND_H__

#include <sys/types.h>

#include <murphy/common/hashtbl.h>
#include <murphy/resource/data-types.h>

#include "plugin-gam-resource-manager.h"

/* positive values are considered to be resource attribute indices */
#define MRP_RESMGR_RESOURCE_FIELD_STATE        -1
#define MRP_RESMGR_RESOURCE_FIELD_DECISION_ID  -2

#define MRP_RESMGR_RESOURCE_FIELD_INVALID (~(uint32_t)0)


mrp_resmgr_backend_t *mrp_resmgr_backend_create(mrp_resmgr_t *resmgr);
void mrp_resmgr_backend_destroy(mrp_resmgr_backend_t *backend);
const char **mrp_resmgr_backend_get_decision_names(void);

uint32_t mrp_resmgr_backend_get_resource_connid(mrp_resmgr_resource_t *ar);
uint32_t mrp_resmgr_backend_get_resource_connno(mrp_resmgr_resource_t *ar);
int32_t mrp_resmgr_backend_get_resource_state(mrp_resmgr_resource_t *ar);
int32_t mrp_resmgr_backend_get_resource_decision_id(mrp_resmgr_resource_t *ar);

mrp_resmgr_resource_t *mrp_resmgr_backend_resource_list_entry(
                                                   mrp_list_hook_t *entry);

uint32_t mrp_resmgr_backend_get_attribute_index(const char *name,
                                                mqi_data_type_t type);
int32_t mrp_resmgr_backend_get_integer_attribute(mrp_resmgr_resource_t *ar,
                                                 uint32_t idx);
const char *mrp_resmgr_backend_get_string_attribute(mrp_resmgr_resource_t *ar,
                                                    uint32_t idx);

#if 0
int mrp_resmgr_backend_print(mrp_resmgr_backend_t *backend, uint32_t zoneid,
                             char *buf, int len);
#endif

#endif /* __MURPHY_GAM_RESOURCE_MANAGER_BACKEND_H__ */
