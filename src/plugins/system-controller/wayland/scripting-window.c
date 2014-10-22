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
#include "window.h"
#include "area.h"

#define WINDOW_CLASS       MRP_LUA_CLASS_SIMPLE(window)
#define WINDOW_MASK_CLASS  MRP_LUA_CLASS_SIMPLE(window_mask)
#define WINDOW_MAP_CLASS   MRP_LUA_CLASS_SIMPLE(window_map)
#define WINDOW_HINT_CLASS  MRP_LUA_CLASS_SIMPLE(window_hint)

typedef struct scripting_window_s  scripting_window_t;
typedef struct scripting_window_mask_s  scripting_window_mask_t;
typedef struct scripting_window_map_s  scripting_window_map_t;
typedef struct mrp_wayland_window_update_s  scripting_window_hint_t;

struct scripting_window_s {
    mrp_wayland_window_t *win;
};

struct scripting_window_mask_s {
    uint32_t mask;
};

struct scripting_window_map_s {
    mrp_wayland_window_map_t *map;
};


static int  window_create_from_lua(lua_State *);
static int  window_getfield(lua_State *);
static int  window_setfield(lua_State *);
static int  window_stringify(lua_State *);
static void window_destroy_from_lua(void *);

static scripting_window_t *window_check(lua_State *L, int idx);

static int  window_mask_create_from_lua(lua_State *);
static int  window_mask_getfield(lua_State *);
static int  window_mask_setfield(lua_State *);
static int  window_mask_stringify(lua_State *);
static int  window_mask_tointeger(lua_State *);
static void window_mask_destroy(void *);

static scripting_window_mask_t *window_mask_check(lua_State *, int);

static uint32_t get_window_mask(mrp_wayland_scripting_field_t);

static int  window_map_create_from_lua(lua_State *);
static int  window_map_getfield(lua_State *);
static int  window_map_setfield(lua_State *);
static int  window_map_stringify(lua_State *);
static void window_map_destroy(void *);

static scripting_window_map_t *window_map_check(lua_State *, int);
static int window_map_push(lua_State *, mrp_wayland_window_map_t *);

static int  window_hint_create_from_lua(lua_State *);
static int  window_hint_getfield(lua_State *);
static int  window_hint_setfield(lua_State *);
static int  window_hint_stringify(lua_State *);
static void window_hint_destroy(void *);

static scripting_window_hint_t *window_hint_check(lua_State *, int);



MRP_LUA_CLASS_DEF_SIMPLE (
    window,                     /* class name */
    scripting_window_t,         /* userdata type */
    window_destroy_from_lua,    /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (window_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (window_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (window_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (window_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (window_stringify)
    )
);

MRP_LUA_CLASS_DEF_SIMPLE_FLAGS (
    window_mask,                /* class name */
    scripting_window_mask_t,    /* userdata type */
    window_mask_destroy,        /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (window_mask_create_from_lua)
       MRP_LUA_METHOD(tointeger,   window_mask_tointeger)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (window_mask_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (window_mask_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (window_mask_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (window_mask_stringify)
    ),
    MRP_LUA_CLASS_DYNAMIC
);

MRP_LUA_CLASS_DEF_SIMPLE (
    window_map,                 /* class name */
    scripting_window_map_t,     /* userdata type */
    window_map_destroy,         /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (window_map_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (window_map_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (window_map_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (window_map_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (window_map_stringify)
    )
);

MRP_LUA_CLASS_DEF_SIMPLE (
    window_hint,                /* class name */
    scripting_window_hint_t,    /* userdata type */
    window_hint_destroy,        /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (window_hint_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (window_hint_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (window_hint_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (window_hint_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (window_hint_stringify)
    )
);



void mrp_wayland_scripting_window_init(lua_State *L)
{
    MRP_ASSERT(L, "invalid argument");

    mrp_lua_create_object_class(L, WINDOW_CLASS);
    mrp_lua_create_object_class(L, WINDOW_MASK_CLASS);
    mrp_lua_create_object_class(L, WINDOW_MAP_CLASS);
    mrp_lua_create_object_class(L, WINDOW_HINT_CLASS);
}

mrp_wayland_window_t *mrp_wayland_scripting_window_check(lua_State *L, int idx)
{
    scripting_window_t *w;
    mrp_wayland_window_t *win;

    if ((w = window_check(L, idx)) && (win = w->win)) {
        MRP_ASSERT(win->scripting_data == (void *)w,
                   "confused with data structures");
        return win;
    }

    return NULL;
}

mrp_wayland_window_t *mrp_wayland_scripting_window_unwrap(void *void_w)
{
    scripting_window_t *w = (scripting_window_t *)void_w;

    if (w && mrp_lua_get_object_classdef(w) == WINDOW_CLASS)
        return w->win;

    return NULL;
}


void *mrp_wayland_scripting_window_create_from_c(lua_State *L,
                                                 mrp_wayland_window_t *win)
{
    scripting_window_t *w;

    MRP_ASSERT(win, "invald argument");
    
    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't create scripting window %d: LUA is "
                      "not initialized", win->surfaceid);
        return NULL;
    }

    w = (scripting_window_t *)mrp_lua_create_object(L, WINDOW_CLASS, NULL,
                                                    win->surfaceid + 1);
    if (!w) {
        mrp_log_error("can't create scripting window %d: LUA object "
                      "creation failed", win->surfaceid);
        return NULL;
    }

    w->win = win;

    return w;
}

void mrp_wayland_scripting_window_destroy_from_c(lua_State *L,
                                                 mrp_wayland_window_t *win)
{
    scripting_window_t *w;
    mrp_wayland_window_map_t *map;

    MRP_ASSERT(win, "invalid argument");

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't destroy scripting window %d: LUA is "
                      "not initialized", win->surfaceid);
        win->scripting_data = NULL;
        return;
    }

    if ((w = win->scripting_data)) {
        mrp_debug("destroy scripting window %d", win->surfaceid);

        if ((map = win->map))
            mrp_wayland_scripting_window_map_destroy_from_c(L, map);

        w->win = NULL;

        mrp_lua_destroy_object(L, NULL,win->surfaceid+1, win->scripting_data);

        win->scripting_data = NULL;
    }
}


static int window_create_from_lua(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "can't create window from lua");

    MRP_LUA_LEAVE(1);
}

static int window_getfield(lua_State *L)
{
    scripting_window_t *w;
    mrp_wayland_window_t *win;
    const char *fldnam;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    fld = mrp_wayland_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    w = window_check(L, 1);

    if (!w || !(win = w->win))
        lua_pushnil(L);
    else {
        switch (fld) {
        case SURFACE:   lua_pushinteger(L, win->surfaceid);              break;
        case NAME:      lua_pushstring(L, win->name ? win->name : "");   break;
        case APPID:     lua_pushstring(L, win->appid ? win->appid : ""); break;
        case PID:       lua_pushinteger(L, win->pid);                    break;
        case NODE:      lua_pushinteger(L, win->nodeid);                 break;
        case LAYER:     lua_pushinteger(L, win->layer ?
                                        win->layer->layerid : -1);       break;
        case POS_X:     lua_pushinteger(L, win->x);                      break;
        case POS_Y:     lua_pushinteger(L, win->y);                      break;
        case WIDTH:     lua_pushinteger(L, win->width);                  break;
        case HEIGHT:    lua_pushinteger(L, win->height);                 break;
        case OPACITY:   lua_pushnumber(L, win->opacity);                 break;
        case VISIBLE:   lua_pushinteger(L, win->visible ? 1 : 0);        break;
        case RAISE:     lua_pushinteger(L, win->raise ? 1 : 0);          break;
        case MAPPED:    lua_pushinteger(L, win->mapped ? 1 : 0);         break;
        case MAP:       window_map_push(L, win->map);                    break;
        case ACTIVE:    lua_pushinteger(L, win->active);                 break;
        case LAYERTYPE: lua_pushinteger(L, win->layertype);              break;
        case AREA:      lua_pushstring(L, win->area && win->area->fullname ?
                                       win->area->fullname : "");        break;
        default:        lua_pushnil(L);                                  break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  window_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    window_check(L, 1);
    luaL_error(L, "window objects are read-only");

    MRP_LUA_LEAVE(0);
}

static int  window_stringify(lua_State *L)
{
#define ALL_FIELDS (MRP_WAYLAND_WINDOW_END_MASK - 1)

    scripting_window_t *w;
    mrp_wayland_window_t *win;
    char *p, *e;
    char buf[4096];

    MRP_LUA_ENTER;

    w = window_check(L, 1);

    if (!(win = w->win))
        lua_pushnil(L);
    else {
        e = (p = buf) + sizeof(buf);
        p += snprintf(p, e-p, "window %d", win->surfaceid);
        p += mrp_wayland_window_print(win, ALL_FIELDS, p, e-p);

        lua_pushlstring(L, buf, p-buf);
    }

    MRP_LUA_LEAVE(1);

#undef ALL_FIELDS
}

static void window_destroy_from_lua(void *data)
{
    scripting_window_t *w = (scripting_window_t *)data;

    MRP_LUA_ENTER;

    if (w && w->win) {
        mrp_wayland_window_set_scripting_data(w->win, NULL);
    }

    MRP_LUA_LEAVE_NOARG;
}


static scripting_window_t *window_check(lua_State *L, int idx)
{
    return (scripting_window_t *)mrp_lua_check_object(L, WINDOW_CLASS, idx);
}


uint32_t mrp_wayland_scripting_window_mask_check(lua_State *L, int idx)
{
    scripting_window_mask_t *um = window_mask_check(L, idx);

    return um ? um->mask : 0;
}

uint32_t mrp_wayland_scripting_window_mask_unwrap(void *void_um)
{
    scripting_window_mask_t *um = (scripting_window_mask_t *)void_um;

    return um ? um->mask : 0;
}


void *mrp_wayland_scripting_window_mask_create_from_c(lua_State *L,
                                                      uint32_t mask)
{
    scripting_window_mask_t *um;

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't create scripting window mask: "
                      "LUA is not initialized");
        return NULL;
    }

    um = (scripting_window_mask_t *)mrp_lua_create_object(L, WINDOW_MASK_CLASS,
                                                          NULL, 0);
    if (!um)
        mrp_log_error("can't create window_mask");
    else
        um->mask = mask;

    return um;
}

static int window_mask_create_from_lua(lua_State *L)
{
    size_t fldnamlen;
    const char *fldnam;
    scripting_window_mask_t *um;
    mrp_wayland_scripting_field_t fld;
    uint32_t m;
    uint32_t mask = 0;

    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        fld = mrp_wayland_scripting_field_name_to_type(fldnam, fldnamlen);

        if (fld == MASK)
            mask = lua_tointeger(L, -1);
        else if ((m = get_window_mask(fld)))
            mask |= lua_toboolean(L, -1) ? m : 0;
        else
            luaL_error(L, "bad field '%s'", fldnam);
    }

    um = (scripting_window_mask_t *)mrp_lua_create_object(L, WINDOW_MASK_CLASS,
                                                          NULL, 0);
    if (!um)
        luaL_error(L, "can't create window_mask");

    um->mask = mask;

    MRP_LUA_LEAVE(1);
}

static int window_mask_getfield(lua_State *L)
{
    scripting_window_mask_t *um;
    mrp_wayland_scripting_field_t fld;
    uint32_t m;

    MRP_LUA_ENTER;

    um = window_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 1);

    if (!um)
        lua_pushnil(L);
    else {
        if (fld == MASK)
            lua_pushinteger(L, um->mask);
        else if ((m = get_window_mask(fld)))
            lua_pushboolean(L, (um->mask & m) ? 1 : 0);
        else
            lua_pushnil(L);
    }

    MRP_LUA_LEAVE(1);
}

static int window_mask_setfield(lua_State *L)
{
    scripting_window_mask_t *um;
    uint32_t m;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    um = window_mask_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 2);

    if (um) {
        if (fld == MASK)
            um->mask = lua_tointeger(L, 3);
        else if ((m = get_window_mask(fld))) {
            um->mask &= ~m;

            if (lua_toboolean(L, 3))
                um->mask |= m;
        }
    }

    MRP_LUA_LEAVE(0);
}

static int window_mask_stringify(lua_State *L)
{
    scripting_window_mask_t *um;
    uint32_t m, mask;
    char buf[4096];
    char *p, *e;
    const char *name;


    MRP_LUA_ENTER;

    um = window_mask_check(L, 1);
    e = (p = buf) + sizeof(buf);
    *p = 0;

    mask = um->mask;

    for (m = 1;  mask && m < MRP_WAYLAND_WINDOW_END_MASK && p < e;   m <<= 1) {
        if ((mask & m)) {
            mask &= ~m;

            if ((name = mrp_wayland_window_update_mask_str(m)))
                p += snprintf(p, e-p, "%s%s", p > buf ? " | " : "", name);
        }
    }

    lua_pushstring(L, (p > buf) ? buf : "<empty>");

    MRP_LUA_LEAVE(1);
}

static int window_mask_tointeger(lua_State *L)
{
    scripting_window_mask_t *um;

    MRP_LUA_ENTER;

    um = window_mask_check(L, 1);
    lua_pushinteger(L, um->mask);

    MRP_LUA_LEAVE(1);
}

static void window_mask_destroy(void *data)
{
    scripting_window_mask_t *um = (scripting_window_mask_t *)data;

    MRP_UNUSED(um);
}


static scripting_window_mask_t *window_mask_check(lua_State *L, int idx)
{
    return (scripting_window_mask_t*)mrp_lua_check_object(L, WINDOW_MASK_CLASS,
                                                          idx);
}


static uint32_t get_window_mask(mrp_wayland_scripting_field_t fld)
{
    switch (fld) {
    case SURFACE:   return MRP_WAYLAND_WINDOW_SURFACEID_MASK;
    case APPID:     return MRP_WAYLAND_WINDOW_APPID_MASK;
    case PID:       return MRP_WAYLAND_WINDOW_PID_MASK;
    case NODE:      return MRP_WAYLAND_WINDOW_NODEID_MASK;
    case LAYER:     return MRP_WAYLAND_WINDOW_LAYER_MASK;
    case POS_X:     return MRP_WAYLAND_WINDOW_X_MASK;
    case POS_Y:     return MRP_WAYLAND_WINDOW_Y_MASK;
    case POSITION:  return MRP_WAYLAND_WINDOW_POSITION_MASK;
    case WIDTH:     return MRP_WAYLAND_WINDOW_WIDTH_MASK;
    case HEIGHT:    return MRP_WAYLAND_WINDOW_HEIGHT_MASK;
    case SIZE:      return MRP_WAYLAND_WINDOW_SIZE_MASK;
    case OPACITY:   return MRP_WAYLAND_WINDOW_OPACITY_MASK;
    case VISIBLE:   return MRP_WAYLAND_WINDOW_VISIBLE_MASK;
    case RAISE:     return MRP_WAYLAND_WINDOW_RAISE_MASK;
    case ACTIVE:    return MRP_WAYLAND_WINDOW_ACTIVE_MASK;
    case MAPPED:    return MRP_WAYLAND_WINDOW_MAPPED_MASK;
    case MAP:       return MRP_WAYLAND_WINDOW_MAP_MASK;
    case LAYERTYPE: return MRP_WAYLAND_WINDOW_LAYERTYPE_MASK;
    case AREA:      return MRP_WAYLAND_WINDOW_AREA_MASK;
    default:        return 0;
    }
}


mrp_wayland_window_map_t *mrp_wayland_scripting_window_map_check(lua_State *L,
                                                                 int idx)
{
    scripting_window_map_t *swm = window_map_check(L, idx);

    return swm ? swm->map : NULL;
}

mrp_wayland_window_map_t *mrp_wayland_scripting_window_map_unwrap(void *void_m)
{
    scripting_window_map_t *swm = (scripting_window_map_t *)void_m;

    return swm ? swm->map : NULL;
}


void *mrp_wayland_scripting_window_map_create_from_c(lua_State *L,
                                                 mrp_wayland_window_map_t *map)
{
    scripting_window_map_t *swm;

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't create scripting window map: "
                      "LUA is not initialized");
        return NULL;
    }

    swm = (scripting_window_map_t *)mrp_lua_create_object(L, WINDOW_MAP_CLASS,
                                                          NULL, 0);
    if (!swm)
        mrp_log_error("can't create window_map");
    else {
        swm->map = map;
        map->scripting_data = swm;
    }

    return swm;
}

void mrp_wayland_scripting_window_map_destroy_from_c(lua_State *L,
                                                 mrp_wayland_window_map_t *map)
{
    scripting_window_map_t *swm;

    MRP_ASSERT(map, "invalid argument");

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't destroy scripting window map: "
                      "LUA is not initialized");
        map->scripting_data = NULL;
        return;
    }

    if ((swm = map->scripting_data)) {
        mrp_debug("destroy scripting window map");

        swm->map = NULL;
        
        mrp_lua_destroy_object(L, NULL,0, swm);
        
        map->scripting_data = NULL;
    }
}

static int window_map_create_from_lua(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "window_map can't be created from LUA");

    lua_pushnil(L);

    MRP_LUA_LEAVE(1);
}

static int window_map_getfield(lua_State *L)
{
    scripting_window_map_t *swm;
    mrp_wayland_scripting_field_t fld;
    mrp_wayland_window_map_t *map;

    MRP_LUA_ENTER;

    swm = window_map_check(L, 1);

    if (!swm)
        lua_pushnil(L);
    else {
        fld = mrp_wayland_scripting_field_check(L, 2, NULL);
        map = swm->map;

        lua_pop(L, 1);

        switch (fld) {
        case TYPE:    lua_pushinteger(L, map->type);               break;
        case TARGET:  lua_pushinteger(L, map->target);             break;
        case WIDTH:   lua_pushinteger(L, map->width);              break;
        case HEIGHT:  lua_pushinteger(L, map->height);             break;
        case STRIDE:  lua_pushinteger(L, map->stride);             break;
        case FORMAT:  lua_pushinteger(L, map->format);             break;
        default:      luaL_error(L, "invalid window_map field");   break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int window_map_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "attempt to modify readonly window_map object");

    MRP_LUA_LEAVE(0);
}

static int window_map_stringify(lua_State *L)
{
    scripting_window_map_t *swm;
    mrp_wayland_window_map_t *map;
    char buf[4096];

    MRP_LUA_ENTER;

    swm = window_map_check(L, 1);
    map = swm->map;

    mrp_wayland_window_map_print(map, buf, sizeof(buf));

    lua_pushstring(L, buf);

    MRP_LUA_LEAVE(1);
}

static void window_map_destroy(void *data)
{
    scripting_window_map_t *swm = (scripting_window_map_t *)data;
    mrp_wayland_window_map_t *map;

    MRP_LUA_ENTER;

    if (swm && (map = swm->map)) {
        map->scripting_data = NULL;
        swm->map = NULL;
    }

    MRP_LUA_LEAVE_NOARG;
}


static scripting_window_map_t *window_map_check(lua_State *L, int idx)
{
    return (scripting_window_map_t*)mrp_lua_check_object(L, WINDOW_MAP_CLASS,
                                                         idx);
}

static int window_map_push(lua_State *L, mrp_wayland_window_map_t *map)
{
    scripting_window_map_t *swm;


    if (!map)
        lua_pushnil(L);
    else {
        if (!(swm = map->scripting_data))
            mrp_wayland_scripting_window_map_create_from_c(L, map);
        else
            mrp_lua_push_object(L, swm);
    }

    return 1;
}


void *mrp_wayland_scripting_window_hint_create_from_c(lua_State *L,
                                             mrp_wayland_window_update_t *hint)
{
    scripting_window_hint_t *wh;
    mrp_wayland_window_update_mask_t mask;

    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't create scripting window hint: "
                      "LUA is not initialized");
        return NULL;
    }

    if (!hint) {
        mrp_log_error("can't create scripting window hint: "
                      "missing hint (NULL)");
        return NULL;
    }

    if (hint->surfaceid < 0) {
        mrp_log_error("can't create scripting window hint: "
                      "missing surfaceid");
        return NULL;
    }

    wh = (scripting_window_hint_t *)mrp_lua_create_object(L, WINDOW_HINT_CLASS,
                                                          NULL, 0);
    if (!wh)
        mrp_log_error("can't create window_hint");
    else {
        mask = hint->mask;

        memset(wh, 0, sizeof(scripting_window_hint_t));

        wh->surfaceid = hint->surfaceid;
        wh->name      = hint->name ? mrp_strdup(hint->name) : NULL;
        wh->appid     = hint->appid ? mrp_strdup(hint->appid) : NULL;
        wh->pid       = hint->pid;
        wh->nodeid    = hint->nodeid;
        wh->area      = hint->area;
        wh->active    = hint->active;
        wh->layertype = hint->layertype;

        if ((mask & MRP_WAYLAND_WINDOW_LAYER_MASK) && hint->layer) {
            wh->mask |= MRP_WAYLAND_WINDOW_LAYER_MASK;
            wh->layer = hint->layer;
        }
        if ((mask & MRP_WAYLAND_WINDOW_X_MASK)) {
            wh->mask |= MRP_WAYLAND_WINDOW_X_MASK;
            wh->x = hint->x;
        }
        if ((mask & MRP_WAYLAND_WINDOW_Y_MASK)) {
            wh->mask |= MRP_WAYLAND_WINDOW_Y_MASK;
            wh->y = hint->y;
        }
        if ((mask & MRP_WAYLAND_WINDOW_WIDTH_MASK)) {
            wh->mask |= MRP_WAYLAND_WINDOW_WIDTH_MASK;
            wh->width = hint->width;
        }
        if ((mask & MRP_WAYLAND_WINDOW_HEIGHT_MASK)) {
            wh->mask |= MRP_WAYLAND_WINDOW_HEIGHT_MASK;
            wh->height = hint->height;
        }
        if ((mask & MRP_WAYLAND_WINDOW_VISIBLE_MASK)) {
            wh->mask |= MRP_WAYLAND_WINDOW_VISIBLE_MASK;
            wh->visible = hint->visible;
        }
        if ((mask & MRP_WAYLAND_WINDOW_RAISE_MASK)) {
            wh->mask |= MRP_WAYLAND_WINDOW_RAISE_MASK;
            wh->raise = hint->raise;
        }
    }

    return wh;
}

static int window_hint_create_from_lua(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "window_hint can't be created from LUA");

    lua_pushnil(L);

    MRP_LUA_LEAVE(1);
}

static int window_hint_getfield(lua_State *L)
{
    scripting_window_hint_t *wh;
    const char *fldnam;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    fld = mrp_wayland_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    wh = window_hint_check(L, 1);

    if (!wh)
        lua_pushnil(L);
    else {
        switch (fld) {
        case SURFACE:   lua_pushinteger(L, wh->surfaceid);              break;
        case NAME:      lua_pushstring(L, wh->name ? wh->name : "");    break;
        case APPID:     lua_pushstring(L, wh->appid ? wh->appid : "");  break;
        case PID:       lua_pushinteger(L, wh->pid);                    break;
        case NODE:      lua_pushinteger(L, wh->nodeid);                 break;
        case LAYER:     lua_pushinteger(L, wh->layer ?
                                        wh->layer->layerid : -1);       break;
        case POS_X:     lua_pushinteger(L, wh->x);                      break;
        case POS_Y:     lua_pushinteger(L, wh->y);                      break;
        case WIDTH:     lua_pushinteger(L, wh->width);                  break;
        case HEIGHT:    lua_pushinteger(L, wh->height);                 break;
        case VISIBLE:   lua_pushinteger(L, wh->visible ? 1 : 0);        break;
        case RAISE:     lua_pushinteger(L, wh->raise ? 1 : 0);          break;
        case ACTIVE:    lua_pushinteger(L, wh->active);                 break;
        case LAYERTYPE: lua_pushinteger(L, wh->layertype);              break;
        case AREA:      lua_pushstring(L, wh->area && wh->area->fullname ?
                                       wh->area->fullname : "");        break;
        default:        lua_pushnil(L);                                 break;
        }
    }

    MRP_LUA_LEAVE(1);
}


static int window_hint_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "attempt to modify readonly window_hint object");

    MRP_LUA_LEAVE(0);
}

static int window_hint_stringify(lua_State *L)
{
#define PRINT(m, fmt, args...)                                          \
    if ((wh->mask & MRP_WAYLAND_WINDOW_ ## m ## _MASK) && (p < e)) {    \
        p += snprintf(p, e-p, "\n      " fmt , ## args);                \
    }

    scripting_window_hint_t *wh;
    char *p, *e;
    char buf[4096];

    MRP_LUA_ENTER;

    wh = window_hint_check(L, 1);
    e = (p = buf) + sizeof(buf);

    p += snprintf(p, e-p, "window %d hint", wh->surfaceid);

    PRINT(LAYER, "layer: '%s'", wh->layer ? wh->layer->name : "<not set>");
    PRINT(POSITION, "position: %d,%d", wh->x, wh->y);
    PRINT(SIZE, "size: %dx%d", wh->width, wh->height);
    PRINT(VISIBLE, "visible: %s", wh->visible ? "yes" : "no");
    PRINT(RAISE, "raise: %s", wh->raise ? "yes" : "no");

    lua_pushlstring(L, buf, p-buf);

    MRP_LUA_LEAVE(1);

#undef PRINT
}

static void window_hint_destroy(void *data)
{
    scripting_window_hint_t *wh = (scripting_window_hint_t *)data;

    MRP_LUA_ENTER;

    mrp_free((void *)wh->name);
    mrp_free((void *)wh->appid);

    MRP_LUA_LEAVE_NOARG;
}

static scripting_window_hint_t *window_hint_check(lua_State *L, int idx)
{
    return (scripting_window_hint_t*)mrp_lua_check_object(L, WINDOW_HINT_CLASS,
                                                          idx);
}
