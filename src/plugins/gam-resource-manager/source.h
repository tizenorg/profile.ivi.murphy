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

#ifndef __MURPHY_GAM_RESOURCE_MANAGER_SOURCE_H__
#define __MURPHY_GAM_RESOURCE_MANAGER_SOURCE_H__

#include "plugin-gam-resource-manager.h"
#include "c5-decision-tree.h"


mrp_resmgr_sources_t *mrp_resmgr_sources_create(mrp_resmgr_t *resmgr);
void mrp_resmgr_sources_destroy(mrp_resmgr_sources_t *sources);

mrp_decision_conf_t *mrp_resmgr_sources_get_decision_conf(mrp_resmgr_t *resmgr);

mrp_resmgr_source_t *mrp_resmgr_source_add(mrp_resmgr_t *resmgr,
                                           const char *gam_name,
                                           uint16_t gam_id);
mrp_resmgr_source_t *mrp_resmgr_source_find_by_name(mrp_resmgr_t *resmgr,
                                           const char *gam_name);
mrp_resmgr_source_t *mrp_resmgr_source_find_by_id(mrp_resmgr_t *resmgr,
                                           uint16_t gam_id);

const char *mrp_resmgr_source_get_name(mrp_resmgr_source_t *src);
bool mrp_resmgr_source_get_availability(mrp_resmgr_source_t *src);

mrp_resmgr_resource_t *mrp_resmgr_source_get_resource(mrp_resmgr_source_t *src,
                                           uint32_t connno);

bool mrp_resmgr_source_add_resource(mrp_resmgr_source_t *src,
                                           mrp_list_hook_t *ar_source_link);
int32_t mrp_resmgr_source_make_decision(mrp_resmgr_source_t *src);


#endif /* __MURPHY_GAM_RESOURCE_MANAGER_SOURCE_H__ */
