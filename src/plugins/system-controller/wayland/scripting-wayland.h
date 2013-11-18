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

#ifndef __MURPHY_WAYLAND_SCRIPTING_H__
#define __MURPHY_WAYLAND_SCRIPTING_H__

#include <lua.h>

#include <murphy/core/lua-bindings/lua-json.h>

#include "wayland/wayland.h"

typedef int (*mrp_wayland_json_copy_func_t)(mrp_wayland_t *, void *,
                                            mrp_json_t *, int);

void mrp_wayland_scripting_init(lua_State *L);
char *mrp_wayland_scripting_canonical_name(const char *name, char *buf,
                                                size_t len);

/* scripting-window-manager.c */
void mrp_wayland_scripting_window_manager_init(lua_State *L);

mrp_wayland_t *mrp_wayland_scripting_window_manager_check(lua_State *L,
                                                          int idx);
mrp_wayland_t *mrp_wayland_scripting_window_manager_unwrap(void *wmgr);

/* scripting-animation.c */
void mrp_wayland_scripting_animation_init(lua_State *L);
mrp_wayland_animation_t *mrp_wayland_scripting_animation_check(lua_State *L,
                                                               int idx);
mrp_wayland_animation_t *mrp_wayland_scripting_animation_unwrap(void *void_an);

/* scripting-window.c */
void mrp_wayland_scripting_window_init(lua_State *);

mrp_wayland_window_t *mrp_wayland_scripting_window_check(lua_State *L,int idx);
mrp_wayland_window_t *mrp_wayland_scripting_window_unwrap(void *void_w);
void *mrp_wayland_scripting_window_create_from_c(lua_State *L,
                                                 mrp_wayland_window_t *win);
void mrp_wayland_scripting_window_destroy_from_c(lua_State *L,
                                                 mrp_wayland_window_t *win);

uint32_t mrp_wayland_scripting_window_mask_check(lua_State *L, int idx);
uint32_t mrp_wayland_scripting_window_mask_unwrap(void *void_um);
void *mrp_wayland_scripting_window_mask_create_from_c(lua_State *L,
                                                      uint32_t mask);

/* scripting-output.c */
void mrp_wayland_scripting_output_init(lua_State *L);

mrp_wayland_output_t *mrp_wayland_scripting_output_check(lua_State *L,int idx);
mrp_wayland_output_t *mrp_wayland_scripting_output_unwrap(void *void_o);
void *mrp_wayland_scripting_output_create_from_c(lua_State *L,
                                                 mrp_wayland_output_t *out);
void mrp_wayland_scripting_output_destroy_from_c(lua_State *L,
                                                 mrp_wayland_output_t *out);

void *mrp_wayland_scripting_output_mask_create_from_c(lua_State *L,
                                                      uint32_t mask);

/* scripting-area.c */
void mrp_wayland_scripting_area_init(lua_State *L);

mrp_wayland_area_t *mrp_wayland_scripting_area_check(lua_State *L, int idx);
mrp_wayland_area_t *mrp_wayland_scripting_area_unwrap(void *void_a);
int mrp_wayland_scripting_area_push(lua_State *L, mrp_wayland_area_t *area);
void *mrp_wayland_scripting_area_create_from_c(lua_State *L,
                                               mrp_wayland_area_t *area);
void mrp_wayland_scripting_area_destroy_from_c(lua_State *L,
                                               mrp_wayland_area_t *area);

void *mrp_wayland_scripting_align_mask_create_from_c(lua_State *L,
                                                     uint32_t mask);


/* scripting-layer.c */
void mrp_wayland_scripting_layer_init(lua_State *L);

mrp_wayland_layer_t *mrp_wayland_scripting_layer_check(lua_State *L, int idx);
mrp_wayland_layer_t *mrp_wayland_scripting_layer_unwrap(void *void_l);
void *mrp_wayland_scripting_layer_create_from_c(lua_State *L,
                                                mrp_wayland_layer_t *layer);
void mrp_wayland_scripting_layer_destroy_from_c(lua_State *L,
                                                mrp_wayland_layer_t *layer);

void *mrp_wayland_scripting_layer_mask_create_from_c(lua_State *L,
                                                     uint32_t mask);

/* internal for scripting-window-manager.c files  */
int mrp_wayland_json_integer_copy(mrp_wayland_t *wl, void *uval,
                                  mrp_json_t *jval, int mask);
int mrp_wayland_json_string_copy(mrp_wayland_t *wl, void *uval,
                                 mrp_json_t *jval, int mask);
int mrp_wayland_json_boolean_copy(mrp_wayland_t *wl, void *uval,
                                  mrp_json_t *jval, int mask);
int mrp_wayland_json_layer_copy(mrp_wayland_t *wl, void *uval,
                                mrp_json_t *jval, int mask);
int mrp_wayland_json_output_copy(mrp_wayland_t *wl, void *uval,
                                 mrp_json_t *jval, int mask);
int mrp_wayland_json_area_copy(mrp_wayland_t *wl, void *uval,
                               mrp_json_t *jval, int mask);
int mrp_wayland_json_align_copy(mrp_wayland_t *wl, void *uval,
                                mrp_json_t *jval, int mask);

mrp_wayland_scripting_field_t
mrp_wayland_scripting_field_check(lua_State *, int, const char **);

mrp_wayland_scripting_field_t
mrp_wayland_scripting_field_name_to_type(const char *, ssize_t);


#endif /* __MURPHY_WAYLAND_SCRIPTING_H__ */
