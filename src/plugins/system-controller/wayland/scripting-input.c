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
#include "input.h"
#include "code.h"

#define INPUT_CLASS       MRP_LUA_CLASS_SIMPLE(input)
#define INPUT_MASK_CLASS  MRP_LUA_CLASS_SIMPLE(input_mask)

typedef struct scripting_input_s  scripting_input_t;
typedef struct scripting_input_mask_s  scripting_input_mask_t;

struct scripting_input_s {
    mrp_wayland_input_t *inp;
};

struct scripting_input_mask_s {
    uint32_t mask;
};

static int  input_create_from_lua(lua_State *);
static int  input_getfield(lua_State *);
static int  input_setfield(lua_State *);
static int  input_stringify(lua_State *);
static void input_destroy_from_lua(void *);

static scripting_input_t *input_check(lua_State *L, int idx);

static int codes_push(lua_State *L, scripting_input_t *si);

static int  input_mask_create_from_lua(lua_State *);
static int  input_mask_getfield(lua_State *);
static int  input_mask_setfield(lua_State *);
static int  input_mask_stringify(lua_State *);
static int  input_mask_tointeger(lua_State *);
static void input_mask_destroy(void *);

static scripting_input_mask_t *input_mask_check(lua_State *, int);

static uint32_t get_input_mask(mrp_wayland_scripting_field_t);

MRP_LUA_CLASS_DEF_SIMPLE (
    input,                      /* class name */
    scripting_input_t,          /* userdata type */
    input_destroy_from_lua,     /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (input_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (input_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (input_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (input_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (input_stringify)
    )
);

MRP_LUA_CLASS_DEF_SIMPLE (
    input_mask,                 /* class name */
    scripting_input_mask_t,     /* userdata type */
    input_mask_destroy,         /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (input_mask_create_from_lua)
       MRP_LUA_METHOD(tointeger,   input_mask_tointeger)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (input_mask_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (input_mask_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (input_mask_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (input_mask_stringify)
    )
);


void mrp_wayland_scripting_input_init(lua_State *L)
{
    MRP_ASSERT(L, "invalid argument");

    mrp_lua_create_object_class(L, INPUT_CLASS);
    mrp_lua_create_object_class(L, INPUT_MASK_CLASS);
}

mrp_wayland_input_t *mrp_wayland_scripting_input_check(lua_State *L, int idx)
{
    scripting_input_t *si;
    mrp_wayland_input_t *inp;

    if ((si = input_check(L, idx)) && (inp = si->inp)) {
        MRP_ASSERT(inp->scripting_data == (void *)si,
                   "confused with data structures");
        return inp;
    }

    return NULL;
}

mrp_wayland_input_t *mrp_wayland_scripting_input_unwrap(void *void_si)
{
    scripting_input_t *si = (scripting_input_t *)void_si;

    if (si && mrp_lua_get_object_classdef(si) == INPUT_CLASS)
        return si->inp;

    return NULL;
}

void *mrp_wayland_scripting_input_create_from_c(lua_State *L,
                                                mrp_wayland_input_t *inp)
{
    scripting_input_t *si;

    MRP_ASSERT(inp, "invald argument");
    
    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't create scripting input %d: LUA is "
                      "not initialized", inp->id);
        return NULL;
    }

    si = (scripting_input_t *)mrp_lua_create_object(L, INPUT_CLASS, NULL,
                                                    inp->id + 1);
    if (!si) {
        mrp_log_error("can't create scripting input %d: LUA object "
                      "creation failed", inp->id);
        return NULL;
    }

    si->inp = inp;

    return si;
}

void mrp_wayland_scripting_input_destroy_from_c(lua_State *L,
                                                 mrp_wayland_input_t *inp)
{
    scripting_input_t *si;

    MRP_ASSERT(inp, "invalid argument");

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't destroy scripting input %d: LUA is "
                      "not initialized", inp->id);
        inp->scripting_data = NULL;
        return;
    }

    if ((si = inp->scripting_data)) {
        mrp_debug("destroy scripting input %d", inp->id);

        si->inp = NULL;
        
        mrp_lua_destroy_object(L, NULL,inp->id+1, inp->scripting_data);
        
        inp->scripting_data = NULL;
    }
}


static int input_create_from_lua(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "can't create input from lua");

    MRP_LUA_LEAVE(1);
}

static int input_getfield(lua_State *L)
{
    scripting_input_t *si;
    mrp_wayland_input_t *inp;
    const char *fldnam;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    fld = mrp_wayland_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    si = input_check(L, 1);

    if (!si || !(inp = si->inp))
        lua_pushnil(L);
    else {
        switch (fld) {
        case DEVICE_NAME:
            lua_pushstring(L, (inp->device && inp->device->name) ?
                               inp->device->name : "<unknown>");
            break;
        case DEVICE_ID:
            lua_pushinteger(L, inp->device ? inp->device->id : -1);
            break;
        case TYPE:
            lua_pushstring(L, mrp_wayland_input_type_str(inp->type));
            break;
        case ID:
            lua_pushinteger(L, inp->id);
            break;
        case NAME:
            lua_pushstring(L, inp->name ? inp->name : "<not-set>");
            break;
        case KEYCODE:
            lua_pushinteger(L, inp->keycode);
            break;
        case CODES:
            codes_push(L, si);
            break;
        case PERMANENT:
            lua_pushboolean(L, inp->permanent);
            break;
        case APPID:
            if (inp->appid)
                lua_pushstring(L, inp->appid);
            else
                lua_pushnil(L);
        case CONNECTED:
            lua_pushboolean(L, inp->connected);
        default:
            lua_pushnil(L);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  input_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    input_check(L, 1);
    luaL_error(L, "input objects are read-only");

    MRP_LUA_LEAVE(0);
}

static int  input_stringify(lua_State *L)
{
#define ALL_FIELDS ((MRP_WAYLAND_INPUT_END_MASK - 1) & \
                    (  ~MRP_WAYLAND_INPUT_ID_MASK  )  )

    scripting_input_t *si;
    mrp_wayland_input_t *inp;
    char *p, *e;
    char buf[4096];

    MRP_LUA_ENTER;

    si = input_check(L, 1);

    if (!(inp = si->inp))
        lua_pushnil(L);
    else {
        e = (p = buf) + sizeof(buf);
        p += snprintf(p, e-p, "input %d", inp->id);
        p += mrp_wayland_input_print(inp, ALL_FIELDS, p, e-p);

        lua_pushlstring(L, buf, p-buf);
    }

    MRP_LUA_LEAVE(1);

#undef ALL_FIELDS
}

static void input_destroy_from_lua(void *data)
{
    scripting_input_t *si = (scripting_input_t *)data;

    MRP_LUA_ENTER;

    if (si && si->inp) {
        mrp_wayland_input_set_scripting_data(si->inp, NULL);
    }

    MRP_LUA_LEAVE_NOARG;
}


static scripting_input_t *input_check(lua_State *L, int idx)
{
    return (scripting_input_t *)mrp_lua_check_object(L, INPUT_CLASS, idx);
}

static int codes_push(lua_State *L, scripting_input_t *si)
{
    mrp_wayland_input_t *inp;
    mrp_wayland_code_t *code;
    size_t i;

    MRP_LUA_ENTER;

    if (!si || !(inp = si->inp))
        lua_pushnil(L);
    else {
        lua_createtable(L, inp->ncode, inp->ncode);

        for (i = 0;  i < inp->ncode; i++) {
            code = inp->codes + i;

            lua_pushinteger(L, code->id + 1);
            lua_pushinteger(L, code->state);
            lua_settable(L, -3);

            if (code->name) {
                lua_pushstring(L, code->name);
                lua_pushinteger(L, code->state);
                lua_settable(L, -3);
            }
        }
    }

    MRP_LUA_LEAVE(1);
}

void *mrp_wayland_scripting_input_mask_create_from_c(lua_State *L,
                                                     uint32_t mask)
{
    scripting_input_mask_t *um;

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't create scripting input mask: "
                      "LUA is not initialized");
        return NULL;
    }

    um = (scripting_input_mask_t *)mrp_lua_create_object(L, INPUT_MASK_CLASS,
                                                         NULL, 0);
    if (!um)
        mrp_log_error("can't create input_mask");
    else
        um->mask = mask;

    return um;
}

static int input_mask_create_from_lua(lua_State *L)
{
    size_t fldnamlen;
    const char *fldnam;
    scripting_input_mask_t *um;
    mrp_wayland_scripting_field_t fld;
    uint32_t m;
    uint32_t mask = 0;

    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        fld = mrp_wayland_scripting_field_name_to_type(fldnam, fldnamlen);

        if (fld == MASK)
            mask = lua_tointeger(L, -1);
        else if ((m = get_input_mask(fld)))
            mask |= lua_toboolean(L, -1) ? m : 0;
        else
            luaL_error(L, "bad field '%s'", fldnam);
    }

    um = (scripting_input_mask_t *)mrp_lua_create_object(L, INPUT_MASK_CLASS,
                                                         NULL, 0);
    if (!um)
        luaL_error(L, "can't create input_mask");

    um->mask = mask;

    MRP_LUA_LEAVE(1);
}

static int input_mask_getfield(lua_State *L)
{
    scripting_input_mask_t *um;
    mrp_wayland_scripting_field_t fld;
    uint32_t m;

    MRP_LUA_ENTER;

    um = input_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 1);

    if (!um)
        lua_pushnil(L);
    else {
        if (fld == MASK)
            lua_pushinteger(L, um->mask);
        else if ((m = get_input_mask(fld)))
            lua_pushboolean(L, (um->mask & m) ? 1 : 0);
        else
            lua_pushnil(L);
    }

    MRP_LUA_LEAVE(1);
}

static int input_mask_setfield(lua_State *L)
{
    scripting_input_mask_t *um;
    uint32_t m;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    um = input_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 2);

    if (um) {
        if (fld == MASK)
            um->mask = lua_tointeger(L, 3);
        else if ((m = get_input_mask(fld))) {
            um->mask &= ~m;

            if (lua_toboolean(L, 3))
                um->mask |= m;
        }
    }

    MRP_LUA_LEAVE(0);
}

static int input_mask_stringify(lua_State *L)
{
    scripting_input_mask_t *um;
    uint32_t m, mask;
    char buf[4096];
    char *p, *e;
    const char *name;


    MRP_LUA_ENTER;

    um = input_mask_check(L, 1);
    e = (p = buf) + sizeof(buf);
    *p = 0;

    mask = um->mask;

    for (m = 1;  mask && m < MRP_WAYLAND_INPUT_END_MASK && p < e;   m <<= 1) {
        if ((mask & m)) {
            mask &= ~m;

            if ((name = mrp_wayland_input_update_mask_str(m)))
                p += snprintf(p, e-p, "%s%s", p > buf ? " | " : "", name);
        }
    }

    lua_pushstring(L, (p > buf) ? buf : "<empty>");

    MRP_LUA_LEAVE(1);
}

static int input_mask_tointeger(lua_State *L)
{
    scripting_input_mask_t *um;

    MRP_LUA_ENTER;

    um = input_mask_check(L, 1);
    lua_pushinteger(L, um->mask);

    MRP_LUA_LEAVE(1);
}

static void input_mask_destroy(void *data)
{
    scripting_input_mask_t *um = (scripting_input_mask_t *)data;

    MRP_UNUSED(um);
}


static scripting_input_mask_t *input_mask_check(lua_State *L, int idx)
{
    return (scripting_input_mask_t*)mrp_lua_check_object(L, INPUT_MASK_CLASS,
                                                         idx);
}

static uint32_t get_input_mask(mrp_wayland_scripting_field_t fld)
{
    switch (fld) {
    case DEVICE_NAME: return MRP_WAYLAND_INPUT_DEVICE_NAME_MASK;
    case DEVICE_ID:   return MRP_WAYLAND_INPUT_DEVICE_ID_MASK;
    case TYPE:        return MRP_WAYLAND_INPUT_TYPE_MASK;
    case ID:          return MRP_WAYLAND_INPUT_ID_MASK;
    case NAME:        return MRP_WAYLAND_INPUT_NAME_MASK;
    case CODES:       return MRP_WAYLAND_INPUT_CODES_MASK;
    default:          return 0;
    }
}
