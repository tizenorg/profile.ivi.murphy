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

#ifndef __MURPHY_SYSTEM_CONTROLLER_RESOURCE_MANAGER_SCRIPTING_H__
#define __MURPHY_SYSTEM_CONTROLLER_RESOURCE_MANAGER_SCRIPTING_H__

#include <lua.h>

#include "resource-manager/resource-manager.h"

void mrp_resmgr_scripting_init(lua_State *L);

mrp_resmgr_t *mrp_resmgr_scripting_check(lua_State *L, int idx);
mrp_resmgr_t *mrp_resmgr_scripting_unwrap(void *void_rm);

/* scripting-notifier.c */
void  mrp_resmgr_scripting_notifier_init(lua_State *L);
void *mrp_resmgr_scripting_screen_event_create_from_c(lua_State *L,
                                                    mrp_resmgr_event_t *event);


/* internal for scripting-xxx.c */
mrp_resmgr_scripting_field_t
mrp_resmgr_scripting_field_check(lua_State *, int, const char **);

mrp_resmgr_scripting_field_t
mrp_resmgr_scripting_field_name_to_type(const char *, ssize_t);


#endif /* __MURPHY_SYSTEM_CONTROLLER_RESOURCE_MANAGER_SCRIPTING_H__ */