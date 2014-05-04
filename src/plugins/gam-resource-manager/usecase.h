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

#ifndef __MURPHY_GAM_RESOURCE_MANAGER_USECASE_H__
#define __MURPHY_GAM_RESOURCE_MANAGER_USECASE_H__

#include "plugin-gam-resource-manager.h"

mrp_resmgr_usecase_t *mrp_resmgr_usecase_create(mrp_resmgr_t *resmgr);
void mrp_resmgr_usecase_destroy(mrp_resmgr_usecase_t *usecase);

ssize_t mrp_resmgr_usecase_add_attribute(mrp_resmgr_usecase_t *usecase,
                                         mrp_resmgr_source_t *source,
                                         const char *name);

void mrp_resmgr_usecase_update(mrp_resmgr_usecase_t *usecase);
void *mrp_resmgr_usecase_get_decision_input(mrp_resmgr_usecase_t *usecase);


size_t mrp_usecase_print(mrp_resmgr_usecase_t *usecase, char *buf, size_t len);



#endif /* __MURPHY_GAM_RESOURCE_MANAGER_USECASE_H__ */
