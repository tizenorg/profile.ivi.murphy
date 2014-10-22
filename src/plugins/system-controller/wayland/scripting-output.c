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
#include "output.h"

#define OUTPUT_CLASS           MRP_LUA_CLASS_SIMPLE(output)
#define OUTPUT_MASK_CLASS      MRP_LUA_CLASS_SIMPLE(output_mask)


typedef struct scripting_output_s  scripting_output_t;
typedef struct scripting_output_mask_s  scripting_output_mask_t;



struct scripting_output_s {
    mrp_wayland_output_t *out;
};

struct scripting_output_mask_s {
    uint32_t mask;
};



static int  output_create_from_lua(lua_State *);
static int  output_getfield(lua_State *);
static int  output_setfield(lua_State *);
static int  output_stringify(lua_State *);
static void output_destroy_from_lua(void *);

static scripting_output_t *output_check(lua_State *L, int idx);

static int  output_mask_create_from_lua(lua_State *);
static int  output_mask_getfield(lua_State *);
static int  output_mask_setfield(lua_State *);
static int  output_mask_stringify(lua_State *);
static void output_mask_destroy(void *);

static scripting_output_mask_t *output_mask_check(lua_State *, int);

static uint32_t get_output_mask(mrp_wayland_scripting_field_t);


MRP_LUA_CLASS_DEF_SIMPLE (
    output,                     /* class name */
    scripting_output_t,         /* userdata type */
    output_destroy_from_lua,    /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (output_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (output_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (output_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (output_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (output_stringify)
    )
);

MRP_LUA_CLASS_DEF_SIMPLE_FLAGS (
    output_mask,                /* class name */
    scripting_output_mask_t,    /* userdata type */
    output_mask_destroy,        /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (output_mask_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (output_mask_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (output_mask_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (output_mask_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (output_mask_stringify)
    ),
    MRP_LUA_CLASS_DYNAMIC
);




void mrp_wayland_scripting_output_init(lua_State *L)
{
    MRP_ASSERT(L, "invalid argument");

    mrp_lua_create_object_class(L, OUTPUT_CLASS);
    mrp_lua_create_object_class(L, OUTPUT_MASK_CLASS);
}


mrp_wayland_output_t *mrp_wayland_scripting_output_check(lua_State *L, int idx)
{
    scripting_output_t *o;
    mrp_wayland_output_t *out;

    if ((o = output_check(L, idx)) && (out = o->out)) {
        MRP_ASSERT(out->scripting_data == (void *)o,
                   "confused with data structures");
        return out;
    }

    return NULL;
}

mrp_wayland_output_t *mrp_wayland_scripting_output_unwrap(void *void_o)
{
    scripting_output_t *o = (scripting_output_t *)void_o;

    if (o && mrp_lua_get_object_classdef(o) == OUTPUT_CLASS)
        return o->out;

    return NULL;
}

void *mrp_wayland_scripting_output_create_from_c(lua_State *L,
                                                 mrp_wayland_output_t *out)
{
    scripting_output_t *o;
    mrp_wayland_t *wl;

    MRP_ASSERT(out && out->interface && out->interface->wl, "invald argument");

    wl = out->interface->wl;

    if (!wl->create_scripting_outputs)
        o = NULL;
    else if (out->scripting_data)
        o = out->scripting_data;
    else {
        if (!L && !(L = mrp_lua_get_lua_state())) {
            mrp_log_error("can't create scripting output %d: LUA is "
                          "not initialized", out->outputid);
            return NULL;
        }

        o = (scripting_output_t *)mrp_lua_create_object(L, OUTPUT_CLASS, NULL,
                                                        out->outputid + 1);
        if (!o) {
            mrp_log_error("can't create scripting output %d: LUA object "
                          "creation failed", out->outputid);
        }
        else {
            o->out = out;
            mrp_wayland_output_set_scripting_data(out, o);
            lua_pop(L, 1);
        }
    }

    return o;
}

void mrp_wayland_scripting_output_destroy_from_c(lua_State *L,
                                                 mrp_wayland_output_t *out)
{
    scripting_output_t *o;

    MRP_ASSERT(out, "invalid argument");

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't destroy scripting output %d: LUA is "
                      "not initialized", out->outputid);
        out->scripting_data = NULL;
        return;
    }

    if ((o = out->scripting_data)) {
        mrp_debug("destroy scripting output %d", out->outputid);

         o->out = NULL;

         mrp_lua_destroy_object(L, NULL,out->outputid+1, out->scripting_data);

         out->scripting_data = NULL;
    }
}


static int output_create_from_lua(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "can't create output from lua");

    MRP_LUA_LEAVE(1);
}

static int output_getfield(lua_State *L)
{
    scripting_output_t *o;
    mrp_wayland_output_t *out;
    const char *fldnam;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    fld = mrp_wayland_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    o = output_check(L, 1);

    if (!o || !(out = o->out))
        lua_pushnil(L);
    else {
        switch (fld) {
        case INDEX:        lua_pushinteger(L, out->index);               break;
        case ID:           lua_pushinteger(L, out->outputid);            break;
        case NAME:         lua_pushstring(L, out->outputname ?
                                          out->outputname : "");         break;
        case PIXEL_X:      lua_pushinteger(L, out->pixel_x);             break;
        case PIXEL_Y:      lua_pushinteger(L, out->pixel_y);             break;
        case PIXEL_WIDTH:  lua_pushinteger(L, out->pixel_width);         break;
        case PIXEL_HEIGHT: lua_pushinteger(L, out->pixel_height);        break;
        case WIDTH:        lua_pushinteger(L, out->width);               break;
        case HEIGHT:       lua_pushinteger(L, out->height);              break;
        case MAKE:         lua_pushstring(L, out->make ? out->make:"");  break;
        case MODEL:        lua_pushstring(L, out->model?out->model:"");  break;
        case ROTATE:       lua_pushinteger(L, out->rotate);              break;
        case FLIP:         lua_pushboolean(L, out->flip);                break;
        default:           lua_pushnil(L);                               break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  output_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    output_check(L, 1);
    luaL_error(L, "output objects are read-only");

    MRP_LUA_LEAVE(0);
}

static int  output_stringify(lua_State *L)
{
#define ALL_FIELDS (MRP_WAYLAND_OUTPUT_END_MASK - 1)

    scripting_output_t *o;
    mrp_wayland_output_t *out;
    char *p, *e;
    char buf[4096];

    MRP_LUA_ENTER;

    o = output_check(L, 1);

    if (!o || !(out = o->out))
        lua_pushnil(L);
    else {
        e = (p = buf) + sizeof(buf);
        p += snprintf(p, e-p, "output %d", out->index);
        p += mrp_wayland_output_print(out, ALL_FIELDS, p, e-p);

        lua_pushlstring(L, buf, p-buf);
    }

    MRP_LUA_LEAVE(1);

#undef ALL_FIELDS
}

static void output_destroy_from_lua(void *data)
{
    scripting_output_t *o = (scripting_output_t *)data;

    MRP_LUA_ENTER;

    if (o && o->out) {
        mrp_wayland_output_set_scripting_data(o->out, NULL);
    }

    MRP_LUA_LEAVE_NOARG;
}


static scripting_output_t *output_check(lua_State *L, int idx)
{
    return (scripting_output_t *)mrp_lua_check_object(L, OUTPUT_CLASS, idx);
}



void *mrp_wayland_scripting_output_mask_create_from_c(lua_State *L,
                                                      uint32_t mask)
{
    scripting_output_mask_t *um;

    um = (scripting_output_mask_t *)mrp_lua_create_object(L, OUTPUT_MASK_CLASS,
                                                          NULL, 0);
    if (!um)
        mrp_log_error("can't create output_mask");
    else
        um->mask = mask;

    return um;
}

static int output_mask_create_from_lua(lua_State *L)
{
    size_t fldnamlen;
    const char *fldnam;
    scripting_output_mask_t *um;
    mrp_wayland_scripting_field_t fld;
    uint32_t m;
    uint32_t mask = 0;

    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        fld = mrp_wayland_scripting_field_name_to_type(fldnam, fldnamlen);

        if (fld == MASK)
            mask = lua_tointeger(L, -1);
        else if ((m = get_output_mask(fld)))
            mask |= m;
        else
            luaL_error(L, "bad field '%s'", fldnam);
    }

    um = (scripting_output_mask_t *)mrp_lua_create_object(L, OUTPUT_MASK_CLASS,
                                                          NULL, 0);
    if (!um)
        luaL_error(L, "can't create output_mask");

    um->mask = mask;

    MRP_LUA_LEAVE(1);
}

static int output_mask_getfield(lua_State *L)
{
    scripting_output_mask_t *um;
    mrp_wayland_scripting_field_t fld;
    uint32_t m;

    MRP_LUA_ENTER;

    um = output_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 1);

    if (!um)
        lua_pushnil(L);
    else {
        if (fld == MASK)
            lua_pushinteger(L, um->mask);
        else if ((m = get_output_mask(fld)))
            lua_pushboolean(L, (um->mask & m) ? 1 : 0);
        else
            lua_pushnil(L);
    }

    MRP_LUA_LEAVE(1);
}

static int output_mask_setfield(lua_State *L)
{
    scripting_output_mask_t *um;
    uint32_t m;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    um = output_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 2);

    if (um) {
        if (fld == MASK)
            um->mask = lua_tointeger(L, 3);
        else if ((m = get_output_mask(fld))) {
            um->mask &= ~m;

            if (lua_toboolean(L, 3))
                um->mask |= m;
        }
    }

    MRP_LUA_LEAVE(0);
}

static int output_mask_stringify(lua_State *L)
{
    scripting_output_mask_t *um;
    uint32_t m, mask;
    char buf[4096];
    char *p, *e;
    const char *name;


    MRP_LUA_ENTER;

    um = output_mask_check(L, 1);
    e = (p = buf) + sizeof(buf);
    *p = 0;

    mask = um->mask;

    for (m = 1;  mask && m < MRP_WAYLAND_OUTPUT_END_MASK && p < e;   m <<= 1) {
        if ((mask & m)) {
            mask &= ~m;

            if ((name = mrp_wayland_output_update_mask_str(m)))
                p += snprintf(p, e-p, "%s%s", p > buf ? " | " : "", name);
        }
    }

    lua_pushstring(L, (p > buf) ? buf : "<empty>");

    MRP_LUA_LEAVE(1);
}

static void output_mask_destroy(void *data)
{
    scripting_output_mask_t *um = (scripting_output_mask_t *)data;

    MRP_UNUSED(um);
}

static scripting_output_mask_t *output_mask_check(lua_State *L, int idx)
{
    return (scripting_output_mask_t *)mrp_lua_check_object(L,OUTPUT_MASK_CLASS,
                                                           idx);
}

static uint32_t get_output_mask(mrp_wayland_scripting_field_t fld)
{
    switch (fld) {
    case ID:           return MRP_WAYLAND_OUTPUT_OUTPUTID_MASK;
    case PIXEL_X:      return MRP_WAYLAND_OUTPUT_PIXEL_X_MASK;
    case PIXEL_Y:      return MRP_WAYLAND_OUTPUT_PIXEL_Y_MASK;
    case PIXEL_WIDTH:  return MRP_WAYLAND_OUTPUT_PIXEL_WIDTH_MASK;
    case PIXEL_HEIGHT: return MRP_WAYLAND_OUTPUT_PIXEL_HEIGHT_MASK;
    case WIDTH:        return MRP_WAYLAND_OUTPUT_WIDTH_MASK;
    case HEIGHT:       return MRP_WAYLAND_OUTPUT_HEIGHT_MASK;
    case SUBPIXEL:     return MRP_WAYLAND_OUTPUT_SUBPIXEL_MASK;
    case MAKE:         return MRP_WAYLAND_OUTPUT_MAKE_MASK;
    case MODEL:        return MRP_WAYLAND_OUTPUT_MODEL_MASK;
    case ROTATE:       return MRP_WAYLAND_OUTPUT_ROTATE_MASK;
    case FLIP:         return MRP_WAYLAND_OUTPUT_FLIP_MASK;
    default:           return 0;
    }
}
