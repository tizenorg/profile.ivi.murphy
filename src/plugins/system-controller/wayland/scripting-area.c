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
#include "area.h"
#include "output.h"

#define AREA_CLASS          MRP_LUA_CLASS_SIMPLE(area)
#define ALIGN_MASK_CLASS    MRP_LUA_CLASS_SIMPLE(align_mask)

#define ALIGN_MASK_INVALID  (~(uint32_t)0)


typedef struct scripting_area_s  scripting_area_t;
typedef struct scripting_align_mask_s  scripting_align_mask_t;


struct scripting_area_s {
    mrp_wayland_area_t *area;
    const char *id;
};

struct scripting_align_mask_s {
    uint32_t mask;
};

static int  area_create_from_lua(lua_State *);
static int  area_getfield(lua_State *);
static int  area_setfield(lua_State *);
static int  area_stringify(lua_State *);
static void area_destroy_from_lua(void *);

static scripting_area_t *area_check(lua_State *, int);

static int  align_mask_create_from_lua(lua_State *);
static int  align_mask_getfield(lua_State *);
static int  align_mask_setfield(lua_State *);
static int  align_mask_stringify(lua_State *);
static void align_mask_destroy(void *);

static scripting_align_mask_t *align_mask_check(lua_State *, int);

static uint32_t get_align_mask(mrp_wayland_scripting_field_t);


MRP_LUA_CLASS_DEF_SIMPLE (
    area,                       /* class name */
    scripting_area_t,           /* userdata type */
    area_destroy_from_lua,      /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (area_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (area_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (area_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (area_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (area_stringify)
    )
);

MRP_LUA_CLASS_DEF_SIMPLE (
    align_mask,                 /* class name */
    scripting_align_mask_t ,    /* userdata type */
    align_mask_destroy,         /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (align_mask_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (align_mask_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (align_mask_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (align_mask_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (align_mask_stringify)
    )
);

void mrp_wayland_scripting_area_init(lua_State *L)
{
    mrp_lua_create_object_class(L, AREA_CLASS);
    mrp_lua_create_object_class(L, ALIGN_MASK_CLASS);
}


mrp_wayland_area_t *mrp_wayland_scripting_area_check(lua_State *L, int idx)
{
    scripting_area_t *a;
    mrp_wayland_area_t *area;

    if ((a = area_check(L, idx)) && (area = a->area)) {
        MRP_ASSERT(area->scripting_data == (void *)a,
                   "confused with data structures");
        return area;
    }

    return NULL;
}

mrp_wayland_area_t *mrp_wayland_scripting_area_unwrap(void *void_a)
{
    scripting_area_t *a = (scripting_area_t *)void_a;

    if (a && mrp_lua_get_object_classdef(a) == AREA_CLASS)
        return a->area;

    return NULL;
}

int mrp_wayland_scripting_area_push(lua_State *L, mrp_wayland_area_t *area)
{
    MRP_LUA_ENTER;

    if (!area || !area->scripting_data)
        lua_pushnil(L);
    else
        mrp_lua_push_object(L, area->scripting_data);

    MRP_LUA_LEAVE(1);
}

void *mrp_wayland_scripting_area_create_from_c(lua_State *L,
                                               mrp_wayland_area_t *area)
{
    scripting_area_t *a;
    char *id;
    char buf[2048];

    MRP_ASSERT(area && area->fullname, "invald argument");
    
    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't create scripting area %d: LUA is "
                      "not initialized", area->areaid);
        return NULL;
    }

    id = mrp_wayland_scripting_canonical_name(area->fullname, buf,sizeof(buf));
    a = (scripting_area_t*)mrp_lua_create_object(L, AREA_CLASS, id, 0);
 
    if (!a) {
        mrp_log_error("can't create scripting area %d: LUA object "
                      "creation failed", area->areaid);
        return NULL;
    }

    a->area = area;
    a->id = mrp_strdup(id);

    return a;
}

void mrp_wayland_scripting_area_destroy_from_c(lua_State *L,
                                               mrp_wayland_area_t *area)
{
    scripting_area_t *a;

    MRP_ASSERT(area, "invalid argument");

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't destroy scripting area %d: LUA is "
                      "not initialized", area->areaid);
        area->scripting_data = NULL;
        return;
    }

    if ((a = area->scripting_data)) {
        mrp_debug("destroy scripting area %d", area->areaid);

         a->area = NULL;

         mrp_lua_destroy_object(L, a->id,0, area->scripting_data);

         area->scripting_data = NULL;
    }
}


static int area_create_from_lua(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "area from lua can only be created via the 'areas' field "
               "of 'outputs' window_manager property");

    MRP_LUA_LEAVE(1);
}

static int area_getfield(lua_State *L)
{
    scripting_area_t *a;
    mrp_wayland_area_t *area;
    const char *fldnam;
    mrp_wayland_scripting_field_t fld;
    char buf[32];

    MRP_LUA_ENTER;

    fld = mrp_wayland_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    a = area_check(L, 1);

    if (!a || !(area = a->area))
        lua_pushnil(L);
    else {
        switch (fld) {
        case ID:
            lua_pushinteger(L, area->areaid);
            break;

        case OUTPUT:
            if (!area->output)
                lua_pushnil(L);
            else if (area->output->outputname)
                lua_pushstring(L, area->output->outputname);
            else {
                snprintf(buf, sizeof(buf), "%d", area->output->outputid);
                lua_pushstring(L, buf);
            }
            break;

        case POS_X:
            lua_pushinteger(L, area->x);
            break;
        case POS_Y:
            lua_pushinteger(L, area->y);
            break;
                                                           
        case WIDTH:
            lua_pushinteger(L, area->width);
            break;
        case HEIGHT:
            lua_pushinteger(L, area->height);
            break;

        case KEEPRATIO:
            lua_pushboolean(L, area->keepratio);
            break;

        case ALIGN:
            mrp_wayland_scripting_align_mask_create_from_c(L, area->align);
            break;

        default:
            lua_pushnil(L);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  area_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    area_check(L, 1);
    luaL_error(L, "area objects are read-only");

    MRP_LUA_LEAVE(0);
}

static int  area_stringify(lua_State *L)
{
#define ALL_FIELDS (MRP_WAYLAND_AREA_END_MASK - 1)

    scripting_area_t *a;
    mrp_wayland_area_t *area;
    char *p, *e;
    char buf[4096];

    MRP_LUA_ENTER;

    a = area_check(L, 1);

    if (!a || !(area = a->area))
        lua_pushnil(L);
    else {
        e = (p = buf) + sizeof(buf);
        p += snprintf(p, e-p, "area %d", area->areaid);
        p += mrp_wayland_area_print(area, ALL_FIELDS, p, e-p);

        lua_pushlstring(L, buf, p-buf);
    }

    MRP_LUA_LEAVE(1);

#undef ALL_FIELDS
}

static void area_destroy_from_lua(void *data)
{
    scripting_area_t *a = (scripting_area_t *)data;

    MRP_LUA_ENTER;

    if (a && a->area) {
        mrp_wayland_area_set_scripting_data(a->area, NULL);
        mrp_free((void *)a->id);
    }

    MRP_LUA_LEAVE_NOARG;
}


static scripting_area_t *area_check(lua_State *L, int idx)
{
    return (scripting_area_t *)mrp_lua_check_object(L, AREA_CLASS, idx);
}


void *mrp_wayland_scripting_align_mask_create_from_c(lua_State *L,
                                                     uint32_t mask)
{
    scripting_align_mask_t *um;

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't create scripting window mask: "
                      "LUA is not initialized");
        return NULL;
    }

    um = (scripting_align_mask_t *)mrp_lua_create_object(L, ALIGN_MASK_CLASS,
                                                         NULL, 0);
    if (!um)
        mrp_log_error("can't create align_mask");
    else
        um->mask = mask;

    return um;
}


static int align_mask_create_from_lua(lua_State *L)
{
    size_t fldnamlen;
    const char *fldnam, *valstr;
    scripting_align_mask_t *um;
    mrp_wayland_scripting_field_t fld, val;
    uint32_t m;
    uint32_t mask = 0;

    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        fld = mrp_wayland_scripting_field_name_to_type(fldnam, fldnamlen);
        
        switch (fld) {

        case MASK:
            mask = lua_tointeger(L, -1);
            break;

        case HORIZONTAL:
            val = mrp_wayland_scripting_field_check(L, -1, &valstr);
            if ((m = get_align_mask(val)) == ALIGN_MASK_INVALID ||
                (m & ~MRP_WAYLAND_AREA_ALIGN_HMASK))
                luaL_error(L, "invalid horizontal alingment '%s'", valstr);
            else {
                mask &= ~MRP_WAYLAND_AREA_ALIGN_HMASK; 
                mask |= m;
            }
            break;

        case VERTICAL:
            val = mrp_wayland_scripting_field_check(L, -1, &valstr);
            if ((m = get_align_mask(val)) == ALIGN_MASK_INVALID ||
                (m & ~MRP_WAYLAND_AREA_ALIGN_VMASK))
                luaL_error(L, "invalid vertical alingment '%s'", valstr);
            else {
                mask &= ~MRP_WAYLAND_AREA_ALIGN_VMASK; 
                mask |= m;
            }
            break;

        default:
            if ((m = get_align_mask(fld)) == ALIGN_MASK_INVALID)
                luaL_error(L, "bad field '%s'", fldnam);
            else {
                mask &= ~((m & MRP_WAYLAND_AREA_ALIGN_HMASK) ?
                          MRP_WAYLAND_AREA_ALIGN_HMASK :
                          MRP_WAYLAND_AREA_ALIGN_VMASK);
                if (lua_toboolean(L, -1))
                    mask |= m;
            }
            break;
        }
    }

    um = (scripting_align_mask_t *)mrp_lua_create_object(L, ALIGN_MASK_CLASS,
                                                         NULL, 0);
    if (!um)
        luaL_error(L, "can't create window_mask");

    um->mask = mask;

    MRP_LUA_LEAVE(1);
}

static int align_mask_getfield(lua_State *L)
{
    scripting_align_mask_t *um;
    mrp_wayland_scripting_field_t fld;
    uint32_t m;

    MRP_LUA_ENTER;

    um = align_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 1);

    if (!um)
        lua_pushnil(L);
    else {
        switch (fld) {
        case MASK:
            lua_pushinteger(L, um->mask);
            break;
        case HORIZONTAL:
            m = um->mask & MRP_WAYLAND_AREA_ALIGN_HMASK;
            lua_pushstring(L, mrp_wayland_area_align_str(m));
            break;
        case VERTICAL:
            m = um->mask & MRP_WAYLAND_AREA_ALIGN_VMASK;
            lua_pushstring(L, mrp_wayland_area_align_str(m));
            break;
        default:
            if ((m = get_align_mask(fld)) != ALIGN_MASK_INVALID)
                lua_pushboolean(L, (um->mask & m) ? 1 : 0);
            else
                lua_pushnil(L);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int align_mask_setfield(lua_State *L)
{
    scripting_align_mask_t *um;
    const char *fldstr, *valstr;
    uint32_t m;
    mrp_wayland_scripting_field_t fld, val;

    MRP_LUA_ENTER;

    um = align_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, &fldstr);

    lua_pop(L, 2);

    if (um) {
        switch (fld) {
        case MASK:
            um->mask = lua_tointeger(L, 3);
            break;
        case HORIZONTAL:
            val = mrp_wayland_scripting_field_check(L, 3, &valstr);
            if ((m = get_align_mask(val)) == ALIGN_MASK_INVALID ||
                (m & ~MRP_WAYLAND_AREA_ALIGN_HMASK))
                luaL_error(L, "invalid horizontal alingment '%s'", valstr);
            else {
                um->mask &= ~MRP_WAYLAND_AREA_ALIGN_HMASK;
                um->mask |= m;
            }
            break;
        case VERTICAL:
            val = mrp_wayland_scripting_field_check(L, 3, &valstr);
            if ((m = get_align_mask(val)) == ALIGN_MASK_INVALID ||
                (m & ~MRP_WAYLAND_AREA_ALIGN_VMASK))
                luaL_error(L, "invalid vertical alingment '%s'", valstr);
            else {
                um->mask &= ~MRP_WAYLAND_AREA_ALIGN_VMASK;
                um->mask |= m;
            }
            break;
        default:
            if ((m = get_align_mask(fld)) == ALIGN_MASK_INVALID)
                luaL_error(L, "invalid alignement field '%s'", fldstr);
            else {
                um->mask &= ~((m & MRP_WAYLAND_AREA_ALIGN_HMASK) ?
                               MRP_WAYLAND_AREA_ALIGN_HMASK :
                               MRP_WAYLAND_AREA_ALIGN_VMASK);
                if (lua_toboolean(L, 3))
                    um->mask |= m;
            }            
        }
    }

    MRP_LUA_LEAVE(0);
}

static int align_mask_stringify(lua_State *L)
{
    scripting_align_mask_t *um;
    uint32_t m, mask;
    char buf[4096];
    char *p, *e;
    const char *name;


    MRP_LUA_ENTER;

    um = align_mask_check(L, 1);

    e = (p = buf) + sizeof(buf);
    *p = 0;

    mask = um->mask;

    for (m=1; mask && m < MRP_WAYLAND_AREA_ALIGN_END_MASK && p < e;  m <<= 1) {
        if ((mask & m)) {
            mask &= ~m;

            if ((name = mrp_wayland_area_align_str(m)))
                p += snprintf(p, e-p, "%s%s", p > buf ? " | " : "", name);
        }
    }

    lua_pushstring(L, (p > buf) ? buf : "<empty>");

    MRP_LUA_LEAVE(1);
}

static void align_mask_destroy(void *data)
{
    scripting_align_mask_t *um = (scripting_align_mask_t *)data;

    MRP_UNUSED(um);
}

static scripting_align_mask_t *align_mask_check(lua_State *L, int idx)
{
    return (scripting_align_mask_t *)mrp_lua_check_object(L, ALIGN_MASK_CLASS,
                                                          idx);
}


static uint32_t get_align_mask(mrp_wayland_scripting_field_t fld)
{
    switch (fld) {
    case MIDDLE:    return MRP_WAYLAND_AREA_ALIGN_MIDDLE;
    case LEFT:      return MRP_WAYLAND_AREA_ALIGN_LEFT;
    case RIGHT:     return MRP_WAYLAND_AREA_ALIGN_RIGHT;
    case TOP:       return MRP_WAYLAND_AREA_ALIGN_TOP;
    case BOTTOM:    return MRP_WAYLAND_AREA_ALIGN_BOTTOM;
    default:        return ALIGN_MASK_INVALID;
    }
}
