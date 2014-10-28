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
#include "animation.h"

#define ANIMATION_CLASS  MRP_LUA_CLASS_SIMPLE(animation)

typedef struct scripting_animation_s  scripting_animation_t;
typedef struct animation_def_s  animation_def_t;

struct scripting_animation_s {
    mrp_wayland_animation_t *anims;
};


struct animation_def_s {
    const char *name;
    int32_t time;
};


static int  animation_create(lua_State *);
static int  animation_getfield(lua_State *);
static int  animation_setfield(lua_State *);
static void animation_destroy(void *);

static scripting_animation_t *animation_check(lua_State *, int);
static int animation_push_type(lua_State *L, mrp_wayland_animation_t *,
                               mrp_wayland_animation_type_t);

static animation_def_t *animation_def_check(lua_State *, int);
static void animation_def_free(animation_def_t *);


MRP_LUA_CLASS_DEF_SIMPLE (
    animation,                  /* class name */
    scripting_animation_t,      /* userdata type */
    animation_destroy,          /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (animation_create)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (animation_create)
       MRP_LUA_OVERRIDE_GETFIELD  (animation_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (animation_setfield)
    )
);


void mrp_wayland_scripting_animation_init(lua_State *L)
{
    MRP_ASSERT(L, "invalid argument");

    mrp_lua_create_object_class(L, ANIMATION_CLASS);
}

mrp_wayland_animation_t *mrp_wayland_scripting_animation_check(lua_State *L,
                                                               int idx)
{
    scripting_animation_t *an;

    MRP_ASSERT(L, "invalid argument");

    if ((an = animation_check(L, idx)))
        return an->anims;

    return NULL;
}                                        


mrp_wayland_animation_t *mrp_wayland_scripting_animation_unwrap(void *void_an)
{
    scripting_animation_t *an = (scripting_animation_t *)void_an;

    if (an && mrp_lua_get_object_classdef(an) == ANIMATION_CLASS)
        return an->anims;

    return NULL;
}


static int animation_create(lua_State *L)
{
    size_t fldnamlen;
    const char *fldnam;
    scripting_animation_t *an;
    mrp_wayland_animation_t *anims;
    animation_def_t *hide = NULL;
    animation_def_t *show = NULL;
    animation_def_t *move = NULL;
    animation_def_t *resize = NULL;
    animation_def_t *map = NULL;

    MRP_LUA_ENTER;


    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        switch (mrp_wayland_scripting_field_name_to_type(fldnam, fldnamlen)) {
        case HIDE:    hide   = animation_def_check(L, -1);     break;
        case SHOW:    show   = animation_def_check(L, -1);     break;
        case MOVE:    move   = animation_def_check(L, -1);     break;
        case RESIZE:  resize = animation_def_check(L, -1);     break;
        case MAP:     map    = animation_def_check(L, -1);     break;
        default:      luaL_error(L, "bad field '%s'", fldnam); break;
        }
    }

    anims = mrp_wayland_animation_create();

    if (hide) {
        mrp_wayland_animation_set(anims, MRP_WAYLAND_ANIMATION_HIDE,
                                  hide->name, hide->time);
        animation_def_free(hide);
    }
    if (show) {
        mrp_wayland_animation_set(anims, MRP_WAYLAND_ANIMATION_SHOW,
                                  show->name, show->time);
        animation_def_free(show);
    }
    if (move) {
        mrp_wayland_animation_set(anims, MRP_WAYLAND_ANIMATION_MOVE,
                                  move->name, move->time);
        animation_def_free(move);
    }
    if (resize) {
        mrp_wayland_animation_set(anims, MRP_WAYLAND_ANIMATION_RESIZE,
                                  resize->name, resize->time);
        animation_def_free(resize);
    }
    if (map) {
        mrp_wayland_animation_set(anims, MRP_WAYLAND_ANIMATION_MAP,
                                  map->name, map->time);
        animation_def_free(map);
    }

    an = (scripting_animation_t *)mrp_lua_create_object(L, ANIMATION_CLASS,
                                                        NULL, 0);
    if (!an)
        luaL_error(L, "can't create animation");

    an->anims = anims;

    MRP_LUA_LEAVE(1);
}

static int  animation_getfield(lua_State *L)
{
    scripting_animation_t *an;
    mrp_wayland_animation_t *anims;
    mrp_wayland_animation_type_t type;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    an = animation_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);

    lua_pop(L, 1);

    if (!an || !(anims = an->anims))
        lua_pushnil(L);
    else {
        switch (fld) {
        case HIDE:    type = MRP_WAYLAND_ANIMATION_HIDE;     goto push;
        case SHOW:    type = MRP_WAYLAND_ANIMATION_SHOW;     goto push;
        case MOVE:    type = MRP_WAYLAND_ANIMATION_MOVE;     goto push;
        case RESIZE:  type = MRP_WAYLAND_ANIMATION_RESIZE;   goto push;
        case MAP:     type = MRP_WAYLAND_ANIMATION_MAP;      goto push;
        push:         animation_push_type(L, anims, type);   break;
        default:      lua_pushnil(L);                        break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  animation_setfield(lua_State *L)
{
    scripting_animation_t *an;
    mrp_wayland_animation_t *anims;
    mrp_wayland_scripting_field_t fld;
    animation_def_t *def;
    mrp_wayland_animation_type_t type;

    MRP_LUA_ENTER;

    an = animation_check(L, 1);
    fld = mrp_wayland_scripting_field_check(L, 2, NULL);
    def = animation_def_check(L, 3);

    lua_pop(L, 2);

    if (an && (anims = an->anims) && def) {
        switch (fld) {
        case HIDE:    type = MRP_WAYLAND_ANIMATION_HIDE;    goto setfield;
        case SHOW:    type = MRP_WAYLAND_ANIMATION_SHOW;    goto setfield;
        case MOVE:    type = MRP_WAYLAND_ANIMATION_MOVE;    goto setfield;
        case RESIZE:  type = MRP_WAYLAND_ANIMATION_RESIZE;  goto setfield;
        case MAP:     type = MRP_WAYLAND_ANIMATION_MAP;     goto setfield;
        default:                                            break;
        setfield:
            mrp_wayland_animation_set(anims, type, def->name, def->time);
            break;
        }
        animation_def_free(def);
    }
    
    MRP_LUA_LEAVE(0);
}
 
static void animation_destroy(void *data)
{
    scripting_animation_t *an = (scripting_animation_t *)data;

    if (an) {
        mrp_wayland_animation_destroy(an->anims);
    }
}


static scripting_animation_t *animation_check(lua_State *L, int idx)
{
    return (scripting_animation_t*)mrp_lua_check_object(L,ANIMATION_CLASS,idx);
}

static int animation_push_type(lua_State *L, mrp_wayland_animation_t *anims,
                               mrp_wayland_animation_type_t type)
{
    mrp_wayland_animation_t *a;

    MRP_LUA_ENTER;

    if (!anims || type < 0 || type >= MRP_WAYLAND_ANIMATION_MAX)
        lua_pushnil(L);
    else {
        a = anims + type;

        if (type != a->type || !a->name  || a->time < 1)
            lua_pushnil(L);
        else {
            lua_createtable(L, 2, 0);

            lua_pushinteger(L, 1);
            lua_pushstring(L, a->name);
            lua_settable(L, -3);

            lua_pushinteger(L, 2);
            lua_pushinteger(L, a->time);
            lua_settable(L, -3);
        }
    }

    MRP_LUA_LEAVE(1);
}

static animation_def_t *animation_def_check(lua_State *L, int t)
{
    animation_def_t *def;
    size_t i, tlen;
    const char *name = NULL;
    int16_t time = -1;

    t = (t < 0) ? lua_gettop(L) + t + 1 : t;

    luaL_checktype(L, t, LUA_TTABLE);

    if ((tlen = lua_objlen(L, t)) != 2) {
        luaL_error(L,"invalid animation definition: "
                   "expected 2 fields, got %u", tlen);
    }

    for (i = 0;  i < tlen;  i++) {
        lua_pushinteger(L, (int)(i+1));
        lua_gettable(L, t);

        if (i == 0)
            name = lua_isstring(L,-1) ?  mrp_strdup(lua_tostring(L,-1)) : NULL;
        else
            time = lua_tointeger(L,-1);

        lua_pop(L, 1);
    }

    if (!name || time < 1)
        return NULL;

    if (!(def = mrp_allocz(sizeof(animation_def_t))))
        mrp_free((void *)name);
    else {
        def->name = name;
        def->time = time;
    }

    return def;
}


static void animation_def_free(animation_def_t *def)
{
    if (def) {
        mrp_free((void *)def->name);
        mrp_free(def);
    }
}
