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

#define CODE_CLASS       MRP_LUA_CLASS_SIMPLE(code)
#define CODE_MASK_CLASS  MRP_LUA_CLASS_SIMPLE(code_mask)

typedef struct scripting_code_s  scripting_code_t;
typedef struct scripting_code_mask_s  scripting_code_mask_t;

struct scripting_code_s {
    mrp_wayland_code_t *code;
    int32_t index;
};

struct scripting_code_mask_s {
    uint32_t mask;
};

static int  code_create_from_lua(lua_State *);
static int  code_getfield(lua_State *);
static int  code_setfield(lua_State *);
static int  code_stringify(lua_State *);
static void code_destroy_from_lua(void *);

static scripting_code_t *code_check(lua_State *, int);

static int32_t code_make_index(mrp_wayland_code_t *);


static int  code_mask_create_from_lua(lua_State *);
static int  code_mask_getfield(lua_State *);
static int  code_mask_setfield(lua_State *);
static int  code_mask_stringify(lua_State *);
static int  code_mask_tointeger(lua_State *);
static void code_mask_destroy(void *);

static scripting_code_mask_t *code_mask_check(lua_State *, int);

static uint32_t get_code_mask(mrp_wayland_scripting_field_t);

MRP_LUA_CLASS_DEF_SIMPLE (
    code,                       /* class name */
    scripting_code_t,           /* userdata type */
    code_destroy_from_lua,      /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (code_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (code_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (code_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (code_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (code_stringify)
    )
);

MRP_LUA_CLASS_DEF_SIMPLE_FLAGS (
    code_mask,                  /* class name */
    scripting_code_mask_t,      /* userdata type */
    code_mask_destroy,          /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (code_mask_create_from_lua)
       MRP_LUA_METHOD(tointeger,   code_mask_tointeger)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (code_mask_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (code_mask_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (code_mask_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (code_mask_stringify)
    ),
    MRP_LUA_CLASS_DYNAMIC
);


void mrp_wayland_scripting_code_init(lua_State *L)
{
    MRP_ASSERT(L, "invalid argument");

    mrp_lua_create_object_class(L, CODE_CLASS);
    mrp_lua_create_object_class(L, CODE_MASK_CLASS);
}

mrp_wayland_code_t *mrp_wayland_scripting_code_check(lua_State *L, int idx)
{
    scripting_code_t *sc;
    mrp_wayland_code_t *code;

    if ((sc = code_check(L, idx)) && (code = sc->code)) {
        MRP_ASSERT(code->scripting_data == (void *)sc,
                   "confused with data structures");
        return code;
    }

    return NULL;
}

mrp_wayland_code_t *mrp_wayland_scripting_code_unwrap(void *void_sc)
{
    scripting_code_t *sc = (scripting_code_t *)void_sc;

    if (sc && mrp_lua_get_object_classdef(sc) == CODE_CLASS)
        return sc->code;

    return NULL;
}

void *mrp_wayland_scripting_code_create_from_c(lua_State *L,
                                               mrp_wayland_code_t *code)
{
    scripting_code_t *sc;
    int32_t index;

    MRP_ASSERT(code, "invald argument");
    
    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't create scripting code %d: LUA is "
                      "not initialized", code->id);
        return NULL;
    }

    index = code_make_index(code);
    sc = (scripting_code_t *)mrp_lua_create_object(L, CODE_CLASS, NULL, index);
    if (!sc) {
        mrp_log_error("can't create scripting code %d: LUA object "
                      "creation failed", code->id);
        return NULL;
    }

    sc->code = code;
    sc->index = index;

    return sc;
}

void mrp_wayland_scripting_code_destroy_from_c(lua_State *L,
                                               mrp_wayland_code_t *code)
{
    scripting_code_t *sc;

    MRP_ASSERT(code, "invalid argument");

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't destroy scripting code %d: LUA is "
                      "not initialized", code->id);
        code->scripting_data = NULL;
        return;
    }

    if ((sc = code->scripting_data)) {
        mrp_debug("destroy scripting code %d", code->id);

        sc->code = NULL;
        
        mrp_lua_destroy_object(L, NULL,sc->index, code->scripting_data);
        
        code->scripting_data = NULL;
    }
}


static int code_create_from_lua(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "can't create code from lua");

    MRP_LUA_LEAVE(1);
}

static int code_getfield(lua_State *L)
{
    scripting_code_t *sc;
    mrp_wayland_code_t *code;
    mrp_wayland_input_t *inp;
    const char *fldnam;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    fld = mrp_wayland_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    sc = code_check(L, 1);

    if (!sc || !(code = sc->code) || !(inp = code->input))
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
        case INPUT:
            lua_pushinteger(L, inp->id);
            break;
        case ID:
            lua_pushinteger(L, code->id);
            break;
        case NAME:
            lua_pushstring(L, code->name ? code->name : "<not-set>");
            break;
        case TIME:
            lua_pushinteger(L, code->time);
            break;
        case STATE:
            lua_pushinteger(L, code->state);
            break;
        default:
            lua_pushnil(L);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  code_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    code_check(L, 1);
    luaL_error(L, "code objects are read-only");

    MRP_LUA_LEAVE(0);
}

static int  code_stringify(lua_State *L)
{
#define ALL_FIELDS ((MRP_WAYLAND_CODE_END_MASK - 1) & \
                    (  ~MRP_WAYLAND_CODE_ID_MASK  )  )

    scripting_code_t *sc;
    mrp_wayland_code_t *code;
    char *p, *e;
    char buf[4096];

    MRP_LUA_ENTER;

    sc = code_check(L, 1);

    if (!(code = sc->code))
        lua_pushnil(L);
    else {
        e = (p = buf) + sizeof(buf);
        p += snprintf(p, e-p, "code %d", code->id);
        p += mrp_wayland_code_print(code, ALL_FIELDS, p, e-p);

        lua_pushlstring(L, buf, p-buf);
    }

    MRP_LUA_LEAVE(1);

#undef ALL_FIELDS
}

static void code_destroy_from_lua(void *data)
{
    scripting_code_t *sc = (scripting_code_t *)data;

    MRP_LUA_ENTER;

    if (sc && sc->code) {
        mrp_wayland_code_set_scripting_data(sc->code, NULL);
    }

    MRP_LUA_LEAVE_NOARG;
}


static scripting_code_t *code_check(lua_State *L, int idx)
{
    return (scripting_code_t *)mrp_lua_check_object(L, CODE_CLASS, idx);
}

static int32_t code_make_index(mrp_wayland_code_t *code)
{
    mrp_wayland_input_t *inp;
    int32_t index;

    if (code && (inp = code->input))
        index = (inp->id * 1000) + (code->id % 1000) + 1;
    else
        index = 0;

    return index;
}

void *mrp_wayland_scripting_code_mask_create_from_c(lua_State *L,
                                                    uint32_t mask)
{
    scripting_code_mask_t *um;

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't create scripting code mask: "
                      "LUA is not initialized");
        return NULL;
    }

    um = (scripting_code_mask_t *)mrp_lua_create_object(L, CODE_MASK_CLASS,
                                                        NULL, 0);
    if (!um)
        mrp_log_error("can't create code_mask");
    else
        um->mask = mask;

    return um;
}

static int code_mask_create_from_lua(lua_State *L)
{
    size_t fldnamlen;
    const char *fldnam;
    scripting_code_mask_t *um;
    mrp_wayland_scripting_field_t fld;
    uint32_t m;
    uint32_t mask = 0;

    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        fld = mrp_wayland_scripting_field_name_to_type(fldnam, fldnamlen);

        if (fld == MASK)
            mask = lua_tointeger(L, -1);
        else if ((m = get_code_mask(fld)))
            mask |= lua_toboolean(L, -1) ? m : 0;
        else
            luaL_error(L, "bad field '%s'", fldnam);
    }

    um = (scripting_code_mask_t *)mrp_lua_create_object(L, CODE_MASK_CLASS,
                                                        NULL, 0);
    if (!um)
        luaL_error(L, "can't create code_mask");

    um->mask = mask;

    MRP_LUA_LEAVE(1);
}

static int code_mask_getfield(lua_State *L)
{
    scripting_code_mask_t *um;
    mrp_wayland_scripting_field_t fld;
    uint32_t m;

    MRP_LUA_ENTER;

    um = code_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 1);

    if (!um)
        lua_pushnil(L);
    else {
        if (fld == MASK)
            lua_pushinteger(L, um->mask);
        else if ((m = get_code_mask(fld)))
            lua_pushboolean(L, (um->mask & m) ? 1 : 0);
        else
            lua_pushnil(L);
    }

    MRP_LUA_LEAVE(1);
}

static int code_mask_setfield(lua_State *L)
{
    scripting_code_mask_t *um;
    uint32_t m;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    um = code_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 2);

    if (um) {
        if (fld == MASK)
            um->mask = lua_tointeger(L, 3);
        else if ((m = get_code_mask(fld))) {
            um->mask &= ~m;

            if (lua_toboolean(L, 3))
                um->mask |= m;
        }
    }

    MRP_LUA_LEAVE(0);
}

static int code_mask_stringify(lua_State *L)
{
    scripting_code_mask_t *um;
    uint32_t m, mask;
    char buf[4096];
    char *p, *e;
    const char *name;


    MRP_LUA_ENTER;

    um = code_mask_check(L, 1);
    e = (p = buf) + sizeof(buf);
    *p = 0;

    mask = um->mask;

    for (m = 1;  mask && m < MRP_WAYLAND_CODE_END_MASK && p < e;   m <<= 1) {
        if ((mask & m)) {
            mask &= ~m;

            if ((name = mrp_wayland_code_update_mask_str(m)))
                p += snprintf(p, e-p, "%s%s", p > buf ? " | " : "", name);
        }
    }

    lua_pushstring(L, (p > buf) ? buf : "<empty>");

    MRP_LUA_LEAVE(1);
}

static int code_mask_tointeger(lua_State *L)
{
    scripting_code_mask_t *um;

    MRP_LUA_ENTER;

    um = code_mask_check(L, 1);
    lua_pushinteger(L, um->mask);

    MRP_LUA_LEAVE(1);
}

static void code_mask_destroy(void *data)
{
    scripting_code_mask_t *um = (scripting_code_mask_t *)data;

    MRP_UNUSED(um);
}


static scripting_code_mask_t *code_mask_check(lua_State *L, int idx)
{
    return (scripting_code_mask_t*)mrp_lua_check_object(L, CODE_MASK_CLASS,
                                                        idx);
}

static uint32_t get_code_mask(mrp_wayland_scripting_field_t fld)
{
    switch (fld) {
    case DEVICE_NAME:  return MRP_WAYLAND_CODE_DEVICE_MASK;
    case DEVICE_ID:    return MRP_WAYLAND_CODE_DEVICE_MASK;
    case INPUT:        return MRP_WAYLAND_CODE_INPUT_MASK;
    case ID:           return MRP_WAYLAND_CODE_ID_MASK;
    case NAME:         return MRP_WAYLAND_CODE_NAME_MASK;
    case STATE:        return MRP_WAYLAND_CODE_STATE_MASK;
    default:           return 0;
    }
}
