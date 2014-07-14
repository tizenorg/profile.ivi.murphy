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
#include <ctype.h>
#include <errno.h>

#include <murphy/common.h>
#include <murphy/core/lua-bindings/murphy.h>
#include <murphy/core/lua-utils/error.h>
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>

#include <lualib.h>
#include <lauxlib.h>

#include "scripting-wayland.h"
#include "layer.h"

#define LAYER_CLASS       MRP_LUA_CLASS_SIMPLE(layer)
#define LAYER_MASK_CLASS  MRP_LUA_CLASS_SIMPLE(layer_mask)

typedef struct scripting_layer_s  scripting_layer_t;
typedef struct scripting_layer_mask_s  scripting_layer_mask_t;


struct scripting_layer_s {
    mrp_wayland_layer_t *layer;
};

struct scripting_layer_mask_s {
    uint32_t mask;
};


static int  layer_create_from_lua(lua_State *);
static int  layer_getfield(lua_State *);
static int  layer_setfield(lua_State *);
static int  layer_stringify(lua_State *);
static void layer_destroy_from_lua(void *);

static scripting_layer_t *layer_check(lua_State *, int);

static int  layer_mask_create_from_lua(lua_State *);
static int  layer_mask_getfield(lua_State *);
static int  layer_mask_setfield(lua_State *);
static int  layer_mask_stringify(lua_State *);
static int  layer_mask_tointeger(lua_State *);
static void layer_mask_destroy(void *);

static scripting_layer_mask_t *layer_mask_check(lua_State *, int);

static uint32_t get_layer_mask(mrp_wayland_scripting_field_t);


MRP_LUA_CLASS_DEF_SIMPLE (
    layer,                      /* class name */
    scripting_layer_t,          /* userdata type */
    layer_destroy_from_lua,     /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (layer_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (layer_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (layer_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (layer_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (layer_stringify)
    )
);

MRP_LUA_CLASS_DEF_SIMPLE (
    layer_mask,                 /* class name */
    scripting_layer_mask_t,     /* userdata type */
    layer_mask_destroy,         /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (layer_mask_create_from_lua)
       MRP_LUA_METHOD(tointeger,   layer_mask_tointeger)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (layer_mask_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (layer_mask_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (layer_mask_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (layer_mask_stringify)
    )
);



void mrp_wayland_scripting_layer_init(lua_State *L)
{
    MRP_ASSERT(L, "invalid argument");

    mrp_lua_create_object_class(L, LAYER_CLASS);
    mrp_lua_create_object_class(L, LAYER_MASK_CLASS);
}


mrp_wayland_layer_t *mrp_wayland_scripting_layer_check(lua_State *L, int idx)
{
    scripting_layer_t *l;
    mrp_wayland_layer_t *layer;

    if ((l = layer_check(L, idx)) && (layer = l->layer)) {
        MRP_ASSERT(layer->scripting_data == (void *)l,
                   "confused with data structures");
        return layer;
    }

    return NULL;
}

mrp_wayland_layer_t *mrp_wayland_scripting_layer_unwrap(void *void_l)
{
    scripting_layer_t *l = (scripting_layer_t *)void_l;

    if (l && mrp_lua_get_object_classdef(l) == LAYER_CLASS)
        return l->layer;

    return NULL;
}

void *mrp_wayland_scripting_layer_create_from_c(lua_State *L,
                                                mrp_wayland_layer_t *layer)
{
    scripting_layer_t *l;
    mrp_wayland_t *wl;

    MRP_ASSERT(layer && layer->wl, "invald argument");

    wl = layer->wl;

    if (!wl->create_scripting_layers)
        l = NULL;
    else if (layer->scripting_data)
        l = layer->scripting_data;
    else {
        if (!L && !(L = mrp_lua_get_lua_state())) {
            mrp_log_error("can't create scripting layer %d: LUA is "
                          "not initialized", layer->layerid);
            return NULL;
        }

        l = (scripting_layer_t *)mrp_lua_create_object(L, LAYER_CLASS, NULL,
                                                       layer->layerid + 1);
        if (!l) {
            mrp_log_error("can't create scripting layer %d: LUA object "
                          "creation failed", layer->layerid);
        }
        else {
            l->layer = layer;
            mrp_wayland_layer_set_scripting_data(layer, l);
            lua_pop(L, 1);
        }
    }

    return l;
}

void mrp_wayland_scripting_layer_destroy_from_c(lua_State *L,
                                                 mrp_wayland_layer_t *layer)
{
    scripting_layer_t *l;

    MRP_ASSERT(layer, "invalid argument");

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't destroy scripting layer %d: LUA is "
                      "not initialized", layer->layerid);
        layer->scripting_data = NULL;
        return;
    }

    if ((l = layer->scripting_data)) {
        mrp_debug("destroy scripting layer %d", layer->layerid);

         l->layer = NULL;

         mrp_lua_destroy_object(L,NULL,layer->layerid+1,layer->scripting_data);

         layer->scripting_data = NULL;
    }
}


static int layer_create_from_lua(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "layer from lua can only be created via the 'layers' "
               "window_manager property");

    MRP_LUA_LEAVE(1);
}

static int layer_getfield(lua_State *L)
{
    scripting_layer_t *l;
    mrp_wayland_layer_t *layer;
    const char *fldnam;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    fld = mrp_wayland_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    l = layer_check(L, 1);

    if (!l || !(layer = l->layer))
        lua_pushnil(L);
    else {
        switch (fld) {
        case ID:
            lua_pushinteger(L, layer->layerid);
            break;

        case NAME:
            lua_pushstring(L, layer->name ? layer->name : "");
            break;

        case LAYERTYPE:
            lua_pushstring(L, mrp_wayland_layer_type_str(layer->type));
            break;

        case OUTPUTNAME:
            lua_pushstring(L, layer->outputname ? layer->outputname : "");
            break;
                                                           
        case VISIBLE:
            lua_pushboolean(L, layer->visible);
            break;

        default:
            lua_pushnil(L);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  layer_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    layer_check(L, 1);
    luaL_error(L, "layer objects are read-only");

    MRP_LUA_LEAVE(0);
}

static int  layer_stringify(lua_State *L)
{
#define ALL_FIELDS (MRP_WAYLAND_LAYER_END_MASK - 1)

    scripting_layer_t *l;
    mrp_wayland_layer_t *layer;
    char *p, *e;
    char buf[4096];

    MRP_LUA_ENTER;

    l = layer_check(L, 1);

    if (!l || !(layer = l->layer))
        lua_pushnil(L);
    else {
        e = (p = buf) + sizeof(buf);
        p += snprintf(p, e-p, "layer %d", layer->layerid);
        p += mrp_wayland_layer_print(layer, ALL_FIELDS, p, e-p);

        lua_pushlstring(L, buf, p-buf);
    }

    MRP_LUA_LEAVE(1);

#undef ALL_FIELDS
}

static void layer_destroy_from_lua(void *data)
{
    scripting_layer_t *l = (scripting_layer_t *)data;

    MRP_LUA_ENTER;

    if (l && l->layer) {
        mrp_wayland_layer_set_scripting_data(l->layer, NULL);
    }

    MRP_LUA_LEAVE_NOARG;
}


static scripting_layer_t *layer_check(lua_State *L, int idx)
{
    return (scripting_layer_t *)mrp_lua_check_object(L, LAYER_CLASS, idx);
}

void *mrp_wayland_scripting_layer_mask_create_from_c(lua_State *L,
                                                     uint32_t mask)
{
    scripting_layer_mask_t *um;

    um = (scripting_layer_mask_t *)mrp_lua_create_object(L, LAYER_MASK_CLASS,
                                                         NULL, 0);
    if (!um)
        mrp_log_error("can't create layer_mask");
    else
        um->mask = mask;

    return (void *)um;
}

static int layer_mask_create_from_lua(lua_State *L)
{
    size_t fldnamlen;
    const char *fldnam;
    scripting_layer_mask_t *um;
    mrp_wayland_scripting_field_t fld;
    uint32_t m;
    uint32_t mask = 0;

    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        fld = mrp_wayland_scripting_field_name_to_type(fldnam, fldnamlen);

        if (fld == MASK)
            mask = lua_tointeger(L, -1);
        else if ((m = get_layer_mask(fld)))
            mask |= m;
        else
            luaL_error(L, "bad field '%s'", fldnam);
    }

    um = (scripting_layer_mask_t *)mrp_lua_create_object(L, LAYER_MASK_CLASS,
                                                         NULL, 0);
    if (!um)
        luaL_error(L, "can't create layer_mask");

    um->mask = mask;

    MRP_LUA_LEAVE(1);
}

static int layer_mask_getfield(lua_State *L)
{
    scripting_layer_mask_t *um;
    mrp_wayland_scripting_field_t fld;
    uint32_t m;

    MRP_LUA_ENTER;

    um = layer_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 1);

    if (!um)
        lua_pushnil(L);
    else {
        if (fld == MASK)
            lua_pushinteger(L, um->mask);
        else if ((m = get_layer_mask(fld)))
            lua_pushboolean(L, (um->mask & m) ? 1 : 0);
        else
            lua_pushnil(L);
    }

    MRP_LUA_LEAVE(1);
}

static int layer_mask_setfield(lua_State *L)
{
    scripting_layer_mask_t *um;
    uint32_t m;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    um = layer_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 2);

    if (um) {
        if (fld == MASK)
            um->mask = lua_tointeger(L, 3);
        else if ((m = get_layer_mask(fld))) {
            um->mask &= ~m;

            if (lua_toboolean(L, 3))
                um->mask |= m;
        }
    }

    MRP_LUA_LEAVE(0);
}

static int layer_mask_stringify(lua_State *L)
{
    scripting_layer_mask_t *um;
    uint32_t m, mask;
    char buf[4096];
    char *p, *e;
    const char *name;


    MRP_LUA_ENTER;

    um = layer_mask_check(L, 1);
    e = (p = buf) + sizeof(buf);
    *p = 0;

    mask = um->mask;

    for (m = 1;  mask && m < MRP_WAYLAND_LAYER_END_MASK && p < e;   m <<= 1) {
        if ((mask & m)) {
            mask &= ~m;

            if ((name = mrp_wayland_layer_update_mask_str(m)))
                p += snprintf(p, e-p, "%s%s", p > buf ? " | " : "", name);
        }
    }

    lua_pushstring(L, (p > buf) ? buf : "<empty>");

    MRP_LUA_LEAVE(1);
}

static int layer_mask_tointeger(lua_State *L)
{
    scripting_layer_mask_t *um;

    MRP_LUA_ENTER;

    um = layer_mask_check(L, 1);
    lua_pushinteger(L, um->mask);

    MRP_LUA_LEAVE(1);
}

static void layer_mask_destroy(void *data)
{
    scripting_layer_mask_t *um = (scripting_layer_mask_t *)data;

    MRP_UNUSED(um);
}


static scripting_layer_mask_t *layer_mask_check(lua_State *L, int idx)
{
    return (scripting_layer_mask_t *)mrp_lua_check_object(L, LAYER_MASK_CLASS,
                                                          idx);
}

static uint32_t get_layer_mask(mrp_wayland_scripting_field_t fld)
{
    switch (fld) {
    case LAYER:      return MRP_WAYLAND_LAYER_LAYERID_MASK;
    case VISIBLE:    return MRP_WAYLAND_LAYER_VISIBLE_MASK;
    case OUTPUTNAME: return MRP_WAYLAND_LAYER_OUTPUTNAME_MASK;
    default:         return 0;
    }
}
