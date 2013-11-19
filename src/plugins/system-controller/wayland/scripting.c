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
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/core/lua-bindings/lua-json.h>

#include <lualib.h>
#include <lauxlib.h>

#include "scripting.h"
#include "window.h"
#include "layer.h"
#include "animation.h"
#include "output.h"
#include "ico-window-manager.h"

#define WINDOW_MANAGER_CLASS   MRP_LUA_CLASS_SIMPLE(window_manager)
#define ANIMATION_CLASS        MRP_LUA_CLASS_SIMPLE(animation)
#define LAYER_MASK_CLASS       MRP_LUA_CLASS_SIMPLE(layer_mask)
#define WINDOW_MASK_CLASS      MRP_LUA_CLASS_SIMPLE(window_mask)

typedef enum field_e  field_t;

typedef struct scripting_winmgr_s       scripting_winmgr_t;
typedef struct scripting_animation_s    scripting_animation_t;
typedef struct scripting_layer_mask_s   scripting_layer_mask_t;
typedef struct scripting_window_mask_s  scripting_window_mask_t;
typedef struct animation_def_s          animation_def_t;
typedef struct funcbridge_def_s         funcbridge_def_t;
typedef struct request_def_s            request_def_t;

typedef int (*copy_func_t)(mrp_wayland_t *, void *, mrp_json_t *, int);

enum field_e {
    PID = 1,
    HIDE,
    MASK,
    MOVE,
    NODE,
    SIZE,
    SHOW,
    APPID,
    LAYER,
    POS_X,
    POS_Y,
    RAISE,
    WIDTH,
    ACTIVE,
    HEIGHT,
    RESIZE,
    DISPLAY,
    SURFACE,
    VISIBLE,
    POSITION,
    LAYER_CREATE,
    LAYER_UPDATE,
    LAYER_REQUEST,
    WINDOW_UPDATE,
    WINDOW_REQUEST,
};

struct scripting_winmgr_s {
    mrp_wayland_t *wl;
    const char *name;
    const char *display;
    mrp_funcbridge_t *layer_update;
    mrp_funcbridge_t *window_update;
};

struct scripting_animation_s {
    mrp_wayland_animation_t *anims;
};

struct scripting_window_mask_s {
    uint32_t mask;
};

struct scripting_layer_mask_s {
    uint32_t mask;
};

struct animation_def_s {
    const char *name;
    int32_t time;
};

struct funcbridge_def_s {
    const char *name;
    const char *sign;
    mrp_funcbridge_cfunc_t func;
    void *data;
    mrp_funcbridge_t **ptr;
};

struct request_def_s {
    const char *name;
    int offset;
    int mask;
    copy_func_t copy;
};

static int  window_manager_create(lua_State *);
static int  window_manager_getfield(lua_State *);
static int  window_manager_setfield(lua_State *);
static void window_manager_destroy(void *);

static int  animation_create(lua_State *);
static int  animation_getfield(lua_State *);
static int  animation_setfield(lua_State *);
static void animation_destroy(void *);

static int  layer_mask_create_from_lua(lua_State *);
static scripting_layer_mask_t *layer_mask_create_from_c(lua_State*,uint32_t);
static int  layer_mask_getfield(lua_State *);
static int  layer_mask_setfield(lua_State *);
static int  layer_mask_stringify(lua_State *);
static void layer_mask_destroy(void *);

static int  window_mask_create_from_lua(lua_State *);
static scripting_window_mask_t *window_mask_create_from_c(lua_State*,uint32_t);
static int  window_mask_getfield(lua_State *);
static int  window_mask_setfield(lua_State *);
static int  window_mask_stringify(lua_State *);
static void window_mask_destroy(void *);

static int integer_copy(mrp_wayland_t *, void *, mrp_json_t *, int);
static int string_copy(mrp_wayland_t *, void *, mrp_json_t *, int);
static int layer_copy(mrp_wayland_t *, void *, mrp_json_t *, int);

static bool layer_request_bridge(lua_State *, void *,
                                 const char *, mrp_funcbridge_value_t *,
                                 char *, mrp_funcbridge_value_t *);
static void layer_update_callback(mrp_wayland_t *,
                                  mrp_wayland_layer_update_mask_t,
                                  mrp_wayland_layer_t *);

static bool window_request_bridge(lua_State *, void *,
                                  const char *, mrp_funcbridge_value_t *,
                                  char *, mrp_funcbridge_value_t *);
static void window_update_callback(mrp_wayland_t *,
                                   mrp_wayland_window_update_mask_t,
                                   mrp_wayland_window_t *);

static animation_def_t *animation_check(lua_State *, int);
static int animation_push(lua_State *, mrp_wayland_animation_t *,
                          mrp_wayland_animation_type_t);
static void animation_def_free(animation_def_t *);

static field_t field_check(lua_State *, int, const char **);
static field_t field_name_to_type(const char *, size_t);

static char *make_id(const char *, char *, int);

static uint32_t get_layer_mask(field_t);
static uint32_t get_window_mask(field_t);

static bool register_methods(lua_State *);


MRP_LUA_CLASS_DEF_SIMPLE (
    window_manager,             /* class name */
    scripting_winmgr_t,         /* userdata type */
    window_manager_destroy,     /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (window_manager_create)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (window_manager_create)
       MRP_LUA_OVERRIDE_GETFIELD  (window_manager_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (window_manager_setfield)
    )
);

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

MRP_LUA_CLASS_DEF_SIMPLE (
    layer_mask,                 /* class name */
    scripting_layer_mask_t,     /* userdata type */
    layer_mask_destroy,         /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (layer_mask_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (layer_mask_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (layer_mask_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (layer_mask_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (layer_mask_stringify)
    )
);

MRP_LUA_CLASS_DEF_SIMPLE (
    window_mask,                /* class name */
    scripting_window_mask_t,    /* userdata type */
    window_mask_destroy,        /* userdata destructor */
    MRP_LUA_METHOD_LIST (       /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (window_mask_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (window_mask_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (window_mask_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (window_mask_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (window_mask_stringify)
    )
);

static mrp_funcbridge_t *layer_create;
static mrp_funcbridge_t *layer_request;
static mrp_funcbridge_t *window_request;


void mrp_scripting_window_manager_init(lua_State *L)
{
    mrp_lua_create_object_class(L, WINDOW_MANAGER_CLASS);
    mrp_lua_create_object_class(L, ANIMATION_CLASS);
    mrp_lua_create_object_class(L, LAYER_MASK_CLASS);
    mrp_lua_create_object_class(L, WINDOW_MASK_CLASS);
    register_methods(L);

    MRP_UNUSED(string_copy);
}

static int  window_manager_create(lua_State *L)
{
    mrp_context_t *ctx;
    mrp_wayland_t *wl;
    size_t fldnamlen;
    const char *fldnam;
    scripting_winmgr_t *winmgr;
    char *name;
    const char *display = NULL;
    mrp_funcbridge_t *layer_update = NULL;
    mrp_funcbridge_t *window_update = NULL;
    char buf[256];

    MRP_LUA_ENTER;

    ctx = mrp_lua_get_murphy_context();

    MRP_ASSERT(ctx && ctx->ml, "invalid app.context or missing mainloop");

    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        switch (field_name_to_type(fldnam, fldnamlen)) {

        case DISPLAY:
            display = luaL_checkstring(L, -1);
            break;

        case LAYER_UPDATE:
            layer_update = mrp_funcbridge_create_luafunc(L, -1);
            break;

        case WINDOW_UPDATE:
            window_update = mrp_funcbridge_create_luafunc(L, -1);
            break;

        default:
            luaL_error(L, "bad field '%s'", fldnam);
            break;
        }
    }

    if (!display)
        display = getenv("WAYLAND_DISPLAY");

    if (!display)
        display = "wayland-0";

    name = make_id(display, buf, sizeof(buf));

    if (!(wl = mrp_wayland_create(display, ctx->ml)))
        luaL_error(L, "can't create wayland object");

    winmgr = (scripting_winmgr_t*)mrp_lua_create_object(L,WINDOW_MANAGER_CLASS,
                                                        name, 0);
    if (!winmgr)
        luaL_error(L, "can't create window manager on display '%s'", display);

    winmgr->wl = wl;
    winmgr->name = mrp_strdup(name);
    winmgr->display = mrp_strdup(display);
    winmgr->layer_update = layer_update;
    winmgr->window_update = window_update;

    mrp_wayland_output_register(wl);
    mrp_ico_window_manager_register(wl);

    mrp_wayland_register_layer_update_callback(wl, layer_update_callback);
    mrp_wayland_register_window_update_callback(wl, window_update_callback);
    mrp_wayland_set_scripting_data(wl, winmgr);

    mrp_wayland_connect(wl);

    MRP_LUA_LEAVE(1);
}

static int  window_manager_getfield(lua_State *L)
{
    scripting_winmgr_t *wmgr;
    field_t fld;

    MRP_LUA_ENTER;

    fld = field_check(L, 2, NULL);
    lua_pop(L, 1);

    wmgr = (scripting_winmgr_t*)mrp_lua_check_object(L,WINDOW_MANAGER_CLASS,1);

    if (!wmgr)
        lua_pushnil(L);
    else {
        switch (fld) {

        case DISPLAY:
            lua_pushstring(L, wmgr->display);
            break;

        case LAYER_CREATE:
            mrp_funcbridge_push(L, layer_create);
            break;

        case LAYER_REQUEST:
            mrp_funcbridge_push(L, layer_request);
            break;

        case LAYER_UPDATE:
            mrp_funcbridge_push(L, wmgr->layer_update);
            break;

        case WINDOW_REQUEST:
            mrp_funcbridge_push(L, window_request);
            break;

        case WINDOW_UPDATE:
            mrp_funcbridge_push(L, wmgr->window_update);
            break;

        default:
            lua_pushnil(L);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  window_manager_setfield(lua_State *L)
{
    scripting_winmgr_t *wmgr;

    MRP_LUA_ENTER;

    wmgr = (scripting_winmgr_t*)mrp_lua_check_object(L,WINDOW_MANAGER_CLASS,1);
    luaL_error(L, "window manager '%s' is read-only", wmgr->display);

    MRP_LUA_LEAVE(0);
}

static void window_manager_destroy(void *data)
{
    scripting_winmgr_t *winmgr = (scripting_winmgr_t *)data;

    MRP_LUA_ENTER;

    if (winmgr) {
        mrp_free((void *)winmgr->name);
        mrp_free((void *)winmgr->display);
    }

    MRP_LUA_LEAVE_NOARG;
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

    MRP_LUA_ENTER;


    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        switch (field_name_to_type(fldnam, fldnamlen)) {
        case HIDE:    hide   = animation_check(L, -1);         break;
        case SHOW:    show   = animation_check(L, -1);         break;
        case MOVE:    move   = animation_check(L, -1);         break;
        case RESIZE:  resize = animation_check(L, -1);         break;
        default:      luaL_error(L, "bad field '%s'", fldnam); break;
        }
    }

    anims = mrp_wayland_animation_create();

    if (hide) {
        mrp_wayland_animation_set(anims, MRP_WAYLAND_ANIMATION_HIDE,
                                  hide->name, hide->time);
        mrp_free(hide);
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
    field_t fld;

    MRP_LUA_ENTER;

    an  = (scripting_animation_t*)mrp_lua_check_object(L, ANIMATION_CLASS, 1);
    fld = field_check(L, 2, NULL);

    lua_pop(L, 1);

    if (!an || !(anims = an->anims))
        lua_pushnil(L);
    else {
        switch (fld) {
        case HIDE:    type = MRP_WAYLAND_ANIMATION_HIDE;     goto push;
        case SHOW:    type = MRP_WAYLAND_ANIMATION_SHOW;     goto push;
        case MOVE:    type = MRP_WAYLAND_ANIMATION_MOVE;     goto push;
        case RESIZE:  type = MRP_WAYLAND_ANIMATION_RESIZE;   goto push;
        push:         animation_push(L, anims, type);        break;
        default:      lua_pushnil(L);                        break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  animation_setfield(lua_State *L)
{
    scripting_animation_t *an;
    mrp_wayland_animation_t *anims;
    field_t fld;
    animation_def_t *def;
    mrp_wayland_animation_type_t type;

    MRP_LUA_ENTER;

    an  = (scripting_animation_t *)mrp_lua_check_object(L, ANIMATION_CLASS, 1);
    fld = field_check(L, 2, NULL);
    def = animation_check(L, 3);

    lua_pop(L, 2);

    if (an && (anims = an->anims) && def) {
        switch (fld) {
        case HIDE:    type = MRP_WAYLAND_ANIMATION_HIDE;    goto setfield;
        case SHOW:    type = MRP_WAYLAND_ANIMATION_SHOW;    goto setfield;
        case MOVE:    type = MRP_WAYLAND_ANIMATION_MOVE;    goto setfield;
        case RESIZE:  type = MRP_WAYLAND_ANIMATION_RESIZE;  goto setfield;
        default:                                            break;
        setfield:
            mrp_wayland_animation_set(anims, type, def->name,def->time);
            break;
        }
        mrp_free(def);
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

static int layer_mask_create_from_lua(lua_State *L)
{
    size_t fldnamlen;
    const char *fldnam;
    scripting_layer_mask_t *um;
    field_t fld;
    uint32_t m;
    uint32_t mask = 0;

    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        fld = field_name_to_type(fldnam, fldnamlen);

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

static scripting_layer_mask_t *layer_mask_create_from_c(lua_State *L,
                                                        uint32_t mask)
{
    scripting_layer_mask_t *um;

    um = (scripting_layer_mask_t *)mrp_lua_create_object(L, LAYER_MASK_CLASS,
                                                         NULL, 0);
    if (!um)
        mrp_log_error("can't create layer_mask");
    else
        um->mask = mask;

    return um;
}

static int layer_mask_getfield(lua_State *L)
{
    scripting_layer_mask_t *um;
    field_t fld;
    uint32_t m;

    MRP_LUA_ENTER;

    um = (scripting_layer_mask_t*)mrp_lua_check_object(L, LAYER_MASK_CLASS, 1);
    fld = field_check(L, 2, NULL);

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
    field_t fld;

    MRP_LUA_ENTER;

    um = (scripting_layer_mask_t*)mrp_lua_check_object(L, LAYER_MASK_CLASS, 1);
    fld = field_check(L, 2, NULL);

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

    um = (scripting_layer_mask_t*)mrp_lua_check_object(L, LAYER_MASK_CLASS, 1);
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

static void layer_mask_destroy(void *data)
{
    scripting_layer_mask_t *um = (scripting_layer_mask_t *)data;

    MRP_UNUSED(um);
}

static int window_mask_create_from_lua(lua_State *L)
{
    size_t fldnamlen;
    const char *fldnam;
    scripting_window_mask_t *um;
    field_t fld;
    uint32_t m;
    uint32_t mask = 0;

    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        fld = field_name_to_type(fldnam, fldnamlen);

        if (fld == MASK)
            mask = lua_tointeger(L, -1);
        else if ((m = get_window_mask(fld)))
            mask |= m;
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

static scripting_window_mask_t *window_mask_create_from_c(lua_State *L,
                                                          uint32_t mask)
{
    scripting_window_mask_t *um;

    um = (scripting_window_mask_t *)mrp_lua_create_object(L, WINDOW_MASK_CLASS,
                                                          NULL, 0);
    if (!um)
        mrp_log_error("can't create window_mask");
    else
        um->mask = mask;

    return um;
}

static int window_mask_getfield(lua_State *L)
{
    scripting_window_mask_t *um;
    field_t fld;
    uint32_t m;

    MRP_LUA_ENTER;

    um = (scripting_window_mask_t*)mrp_lua_check_object(L,WINDOW_MASK_CLASS,1);
    fld = field_check(L, 2, NULL);

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
    field_t fld;

    MRP_LUA_ENTER;

    um = (scripting_window_mask_t*)mrp_lua_check_object(L,WINDOW_MASK_CLASS,1);
    fld = field_check(L, 2, NULL);

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

    um = (scripting_window_mask_t*)mrp_lua_check_object(L,WINDOW_MASK_CLASS,1);
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

static void window_mask_destroy(void *data)
{
    scripting_window_mask_t *um = (scripting_window_mask_t *)data;

    MRP_UNUSED(um);
}

static int integer_copy(mrp_wayland_t *wl,void *uval,mrp_json_t *jval,int mask)
{
    MRP_UNUSED(wl);

    if (mrp_json_is_type(jval, MRP_JSON_INTEGER)) {
        *(int32_t *)uval = mrp_json_integer_value(jval);
        return mask;
    }

    return 0;
}

static int string_copy(mrp_wayland_t *wl,void *uval,mrp_json_t *jval,int mask)
{
    MRP_UNUSED(wl);

    if (mrp_json_is_type(jval, MRP_JSON_STRING)) {
        *(const char **)uval = mrp_json_string_value(jval);
        return mask;
    }

    return 0;
}

static int layer_copy(mrp_wayland_t *wl,void *uval,mrp_json_t *jval,int mask)
{
    int32_t layerid;
    mrp_wayland_layer_t *layer;

    if (!mrp_json_is_type(jval, MRP_JSON_INTEGER))
        return 0;

    layerid = mrp_json_integer_value(jval);

    if (!(layer = mrp_wayland_layer_find(wl, layerid)))
        return 0;

    *(mrp_wayland_layer_t **)uval = layer;

    return mask;
}

static bool layer_create_bridge(lua_State *L,
                                void *data,
                                const char *signature,
                                mrp_funcbridge_value_t *args,
                                char *ret_type,
                                mrp_funcbridge_value_t *ret_val)
{
    scripting_winmgr_t *winmgr;
    mrp_wayland_t *wl;
    int32_t id;
    const char *name;
    mrp_wayland_layer_update_t u;
    bool success;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    do { /* not a loop */
        *ret_type = MRP_FUNCBRIDGE_NO_DATA;

        success = false;

        if (strcmp(signature, "ods")) {
            mrp_log_error("bad signature: expected 'ods' got '%s'",signature);
            break;
        }

        winmgr = args[0].pointer;
        id = args[1].integer;
        name = args[2].string;

        if (mrp_lua_get_object_classdef(winmgr) != WINDOW_MANAGER_CLASS) {
            mrp_log_error("argument 1 is not a 'window_manager' class object");
            break;
        }

        wl = winmgr->wl;

        MRP_ASSERT(wl, "confused with data structures");

        if (id < 0 || id >= MRP_WAYLAND_LAYER_MAX) {
            mrp_log_error("argument 2 value is %d which is "
                          "out of range (0-%d)", id, MRP_WAYLAND_LAYER_MAX);
            break;
        }

        MRP_ASSERT(name, "arg3 (ie. name) was NULL");

        success = true;

        memset(&u, 0, sizeof(u));
        u.mask = MRP_WAYLAND_LAYER_LAYERID_MASK | MRP_WAYLAND_LAYER_NAME_MASK;
        u.layerid = id;
        u.name = name;

        mrp_wayland_layer_create(wl, &u);

    } while(0);

    return success;
}

static bool layer_request_bridge(lua_State *L,
                                 void *data,
                                 const char *signature,
                                 mrp_funcbridge_value_t *args,
                                 char *ret_type,
                                 mrp_funcbridge_value_t *ret_val)
{
#define FIELD(f) MRP_OFFSET(mrp_wayland_layer_update_t, f)
#define MASK(m)  MRP_WAYLAND_LAYER_ ## m ## _MASK
#define TYPE(t)  t ## _copy

    request_def_t   fields[] = {
        { "layer"  , FIELD(layerid)  , MASK(LAYERID), TYPE(integer) },
        { "visible", FIELD(visible)  , MASK(VISIBLE), TYPE(integer) },
        {   NULL   ,        0        ,      0       ,      NULL     }
    };

#undef TYPE
#undef MASK
#undef FIELD

    scripting_winmgr_t *winmgr;
    mrp_wayland_t *wl;
    mrp_wayland_layer_update_t u;
    mrp_json_t *json;
    const char *key;
    mrp_json_t *val;
    mrp_json_iter_t it;
    request_def_t *f;
    int m;
    bool success;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    do { /* not a loop */
        *ret_type = MRP_FUNCBRIDGE_NO_DATA;

        success = false;

        if (strcmp(signature, "oo")) {
            mrp_log_error("bad signature: expected 'oo' got '%s'",signature);
            break;
        }

        winmgr = args[0].pointer;

        if (mrp_lua_get_object_classdef(winmgr) != WINDOW_MANAGER_CLASS) {
            mrp_log_error("argument 1 is not a 'window_manager' class object");
            break;
        }

        wl = winmgr->wl;

        MRP_ASSERT(wl, "confused with data structures");

        success = true;

        /* TODO: replace this with a function call */
        json = *(mrp_json_t **)args[1].pointer;
        if (!json) {
            mrp_log_error("argument 2 is not a 'JSON' class object");
            break;
        }

        memset(&u, 0, sizeof(u));

        mrp_json_foreach_member(json, key,val, it) {
            for (f = fields;   f->name;   f++) {
                if (!strcmp(key, f->name)) {
                    if ((m = f->copy(wl, (void*)&u + f->offset, val, f->mask)))
                        u.mask |= m;
                    else
                        mrp_debug("'%s' field has invalid value", key);
                    break;
                }
            }
        } /* mrp_json_foreach_member */

        mrp_wayland_layer_request(wl, &u);

    } while(0);

    return success;
}

static void layer_update_callback(mrp_wayland_t *wl,
                                  mrp_wayland_layer_update_mask_t mask,
                                  mrp_wayland_layer_t *layer)
{
    static mrp_wayland_layer_update_mask_t filter =
        MRP_WAYLAND_LAYER_LAYERID_MASK   |
        MRP_WAYLAND_LAYER_VISIBLE_MASK   ;

    lua_State *L;
    scripting_winmgr_t *winmgr;
    mrp_json_t *json;
    mrp_funcbridge_value_t args[3], ret;
    char t;
    bool success;

    MRP_UNUSED(filter);

    MRP_ASSERT(wl && layer, "invalid argument");

    if (!(winmgr = (scripting_winmgr_t *)wl->scripting_data)) {
        mrp_log_error("window manager scripting is not initialized");
        return;
    }

    MRP_ASSERT(wl == winmgr->wl, "confused with data structures");

    if (!mask) {
        mrp_debug("nothing to do");
        return;
    }

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't update layer %u: LUA is not initialesed",
                      layer->layerid);
        return;
    }

    if (!(json = mrp_json_create(MRP_JSON_OBJECT))) {
        mrp_log_error("failed to create JSON object for layer %d update",
                      layer->layerid);
        return;
    }

    if (!mrp_json_add_integer(json, "layer"  , layer->layerid) ||
        !mrp_json_add_string (json, "visible", layer->visible)  )
    {
        mrp_log_error("failed to build JSON object for layer %d update",
                      layer->layerid);
    }

    args[0].pointer = winmgr;
    args[1].pointer = mrp_json_lua_wrap(L, json);
    args[2].pointer = layer_mask_create_from_c(L, mask);

    memset(&ret, 0, sizeof(ret));

    success = mrp_funcbridge_call_from_c(L, winmgr->layer_update, "ooo",
                                         args, &t, &ret);
    if (!success) {
        mrp_log_error("failed to call window_manager.%s.layer_update method "
                      "(%s)", layer->name, ret.string ? ret.string : "NULL");
        mrp_free((void *)ret.string);
    }
}

static bool window_request_bridge(lua_State *L,
                                  void *data,
                                  const char *signature,
                                  mrp_funcbridge_value_t *args,
                                  char *ret_type,
                                  mrp_funcbridge_value_t *ret_val)
{
#define FIELD(f) MRP_OFFSET(mrp_wayland_window_update_t, f)
#define MASK(m)  MRP_WAYLAND_WINDOW_ ## m ## _MASK
#define TYPE(t)  t ## _copy

    request_def_t   fields[] = {
        { "surface", FIELD(surfaceid), MASK(SURFACEID), TYPE(integer) },
        { "layer"  , FIELD(layer)    , MASK(LAYER)    , TYPE(layer)   },
        { "node"   , FIELD(nodeid)   , MASK(NODEID)   , TYPE(integer) },
        { "pos_x"  , FIELD(x)        , MASK(X)        , TYPE(integer) },
        { "pos_y"  , FIELD(y)        , MASK(Y)        , TYPE(integer) },
        { "width"  , FIELD(width)    , MASK(WIDTH)    , TYPE(integer) },
        { "height" , FIELD(height)   , MASK(HEIGHT)   , TYPE(integer) },
        { "raise"  , FIELD(raise)    , MASK(RAISE)    , TYPE(integer) },
        { "visible", FIELD(visible)  , MASK(VISIBLE)  , TYPE(integer) },
        { "active" , FIELD(active)   , MASK(ACTIVE)   , TYPE(integer) },
        {   NULL   ,        0        ,      0         ,      NULL     }
    };

#undef TYPE
#undef MASK
#undef FIELD

    scripting_winmgr_t *winmgr;
    scripting_animation_t *an;
    mrp_wayland_t *wl;
    mrp_wayland_window_update_t u;
    mrp_wayland_animation_t *anims;
    uint32_t framerate;
    mrp_json_t *json;
    const char *key;
    mrp_json_t *val;
    mrp_json_iter_t it;
    request_def_t *f;
    int m;
    bool success;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    do { /* not a loop */
        *ret_type = MRP_FUNCBRIDGE_NO_DATA;

        success = false;

        if (strcmp(signature, "oood")) {
            mrp_log_error("bad signature: expected 'oood' got '%s'",signature);
            break;
        }

        winmgr = args[0].pointer;
        an = args[2].pointer;
        framerate = args[3].integer;

        if (mrp_lua_get_object_classdef(winmgr) != WINDOW_MANAGER_CLASS) {
            mrp_log_error("argument 1 is not a 'window_manager' class object");
            break;
        }
        if (mrp_lua_get_object_classdef(an) != ANIMATION_CLASS) {
            mrp_log_error("argument 2 is not an 'animation' class object");
        }

        wl = winmgr->wl;
        anims = an->anims;

        MRP_ASSERT(wl && anims, "confused with data structures");

        success = true;

        /* TODO: replace this with a function call */
        json = *(mrp_json_t **)args[1].pointer;
        if (!json) {
            mrp_log_error("argument 2 is not a 'JSON' class object");
            break;
        }

        memset(&u, 0, sizeof(u));

        mrp_json_foreach_member(json, key,val, it) {
            for (f = fields;   f->name;   f++) {
                if (!strcmp(key, f->name)) {
                    if ((m = f->copy(wl, (void*)&u + f->offset, val, f->mask)))
                        u.mask |= m;
                    else
                        mrp_debug("'%s' field has invalid value", key);
                    break;
                }
            }
        } /* mrp_json_foreach_member */

        mrp_wayland_window_request(wl, &u, anims, framerate);

    } while(0);

    return success;
}

static void window_update_callback(mrp_wayland_t *wl,
                                   mrp_wayland_window_update_mask_t mask,
                                   mrp_wayland_window_t *win)
{
    static mrp_wayland_window_update_mask_t filter =
        MRP_WAYLAND_WINDOW_SURFACEID_MASK |
        MRP_WAYLAND_WINDOW_APPID_MASK     |
        MRP_WAYLAND_WINDOW_PID_MASK       |
        MRP_WAYLAND_WINDOW_NODEID_MASK    |
        MRP_WAYLAND_WINDOW_LAYER_MASK     |
        MRP_WAYLAND_WINDOW_POSITION_MASK  |
        MRP_WAYLAND_WINDOW_SIZE_MASK      |
        MRP_WAYLAND_WINDOW_VISIBLE_MASK   |
        MRP_WAYLAND_WINDOW_RAISE_MASK     |
        MRP_WAYLAND_WINDOW_ACTIVE_MASK    ;

    lua_State *L;
    scripting_winmgr_t *winmgr;
    mrp_json_t *json;
    int32_t layerid;
    mrp_funcbridge_value_t args[3], ret;
    char t;
    bool success;

    MRP_UNUSED(filter);

    MRP_ASSERT(wl && win, "invalid argument");

    if (!(winmgr = (scripting_winmgr_t *)wl->scripting_data)) {
        mrp_log_error("window manager scripting is not initialized");
        return;
    }

    MRP_ASSERT(wl == winmgr->wl, "confused with data structures");

    if (!mask) {
        mrp_debug("nothing to do");
        return;
    }

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't update window %u: LUA is not initialesed",
                      win->surfaceid);
        return;
    }

    if (!(json = mrp_json_create(MRP_JSON_OBJECT))) {
        mrp_log_error("failed to create JSON object for window %d update",
                      win->surfaceid);
        return;
    }

    layerid = win->layer ? win->layer->layerid : -1;

    if (!mrp_json_add_integer(json, "surface", win->surfaceid) ||
        !mrp_json_add_string (json, "appid"  , win->appid    ) ||
        !mrp_json_add_integer(json, "pid"    , win->pid      ) ||
        !mrp_json_add_integer(json, "node"   , win->nodeid   ) ||
        !mrp_json_add_integer(json, "layer"  , layerid       ) ||
        !mrp_json_add_integer(json, "pos_x"  , win->x        ) ||
        !mrp_json_add_integer(json, "pos_y"  , win->y        ) ||
        !mrp_json_add_integer(json, "width"  , win->width    ) ||
        !mrp_json_add_integer(json, "height" , win->height   ) ||
        !mrp_json_add_integer(json, "visible", win->visible  ) ||
        !mrp_json_add_integer(json, "raise"  , win->raise    ) ||
        !mrp_json_add_integer(json, "active" , win->active   )  )
    {
        mrp_log_error("failed to build JSON object for window %d update",
                      win->surfaceid);
    }

    args[0].pointer = winmgr;
    args[1].pointer = mrp_json_lua_wrap(L, json);
    args[2].pointer = window_mask_create_from_c(L, mask);

    memset(&ret, 0, sizeof(ret));

    success = mrp_funcbridge_call_from_c(L, winmgr->window_update, "ooo",
                                         args, &t, &ret);
    if (!success) {
        mrp_log_error("failed to call window_manager.%s.window_update method "
                      "(%s)", winmgr->name, ret.string ? ret.string : "NULL");
        mrp_free((void *)ret.string);
    }
}


static animation_def_t *animation_check(lua_State *L, int t)
{
    animation_def_t *def;
    size_t i, tlen;
    const char *name;
    int16_t time;

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


static int animation_push(lua_State *L, mrp_wayland_animation_t *anims,
                          mrp_wayland_animation_type_t type)
{
    mrp_wayland_animation_t *a;

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

    return 1;
}

static void animation_def_free(animation_def_t *def)
{
    if (def) {
        mrp_free((void *)def->name);
        mrp_free(def);
    }
}

static field_t field_check(lua_State *L, int idx, const char **ret_fldnam)
{
    const char *fldnam;
    size_t fldnamlen;
    field_t fldtyp;

    if (!(fldnam = lua_tolstring(L, idx, &fldnamlen)))
        fldtyp = 0;
    else
        fldtyp = field_name_to_type(fldnam, fldnamlen);

    if (ret_fldnam)
        *ret_fldnam = fldnam;

    return fldtyp;
}

static field_t field_name_to_type(const char *name, size_t len)
{
    switch (len) {

    case 3:
        if (!strcmp(name, "pid"))
            return PID;
        break;

    case 4:
        switch (name[0]) {
        case 'h':
            if (!strcmp(name, "hide"))
                return HIDE;
            break;
        case 'm':
            if (!strcmp(name, "mask"))
                return MASK;
            break;
            if (!strcmp(name, "move"))
                return MOVE;
            break;
        case 'n':
            if (!strcmp(name, "node"))
                return NODE;
            break;
        case 's':
            if (!strcmp(name, "show"))
                return SHOW;
            if (!strcmp(name, "size"))
                return SIZE;
            break;
        default:
            break;
        }
        break;

    case 5:
        switch(name[0]) {
        case 'a':
            if (!strcmp(name, "appid"))
                return APPID;
            break;
        case 'l':
            if (!strcmp(name, "layer"))
                return LAYER;
            break;
        case 'p':
            if (!strcmp(name, "pos_x"))
                return POS_X;
            if (!strcmp(name, "pos_y"))
                return POS_Y;
            break;
        case 'r':
            if (!strcmp(name, "raise"))
                return RAISE;
            break;
        case 'w':
            if (!strcmp(name, "width"))
                return WIDTH;
            break;
        default:
            break;
        }
        break;

    case 6:
        switch (name[0]) {
        case 'a':
            if (!strcmp(name, "active"))
                return ACTIVE;
            break;
        case 'h':
            if (!strcmp(name, "height"))
                return HEIGHT;
            break;
        case 'r':
            if (!strcmp(name, "resize"))
                return RESIZE;
            break;
        default:
            break;
        }
        break;

    case 7:
        switch (name[0]) {
        case 'd':
            if (!strcmp(name, "display"))
                return DISPLAY;
            break;
        case 's':
            if (!strcmp(name, "surface"))
                return SURFACE;
            break;
        case 'v':
            if (!strcmp(name, "visible"))
                return VISIBLE;
            break;
        default:
            break;
        }
        break;

    case 8:
        if (!strcmp(name, "position"))
            return POSITION;
        break;

    case 12:
        if (!strcmp(name, "layer_create"))
            return LAYER_CREATE;
        if (!strcmp(name, "layer_update"))
            return LAYER_UPDATE;
        break;

    case 13:
        switch (name[0]) {
        case 'l':
            if (!strcmp(name, "layer_request"))
                return LAYER_REQUEST;
            break;
        case 'w':
            if (!strcmp(name, "window_update"))
                return WINDOW_UPDATE;
            break;
        default:
            break;
        }
        break;

    case 14:
        if (!strcmp(name, "window_request"))
            return WINDOW_REQUEST;
        break;

    default:
        break;
    }

    return 0;
}

static char *make_id(const char *string, char *buf, int len)
{
    const char *q;
    char *p, *e, c;

    e = (p = buf) + (len - 1);
    q = string;

    while ((c = *q++) && p < e)
        *p++ = isalnum(c) ? c : '_';

    *p = 0;

    return buf;
}

static uint32_t get_layer_mask(field_t fld)
{
    switch (fld) {
    case LAYER:     return MRP_WAYLAND_LAYER_LAYERID_MASK;
    case VISIBLE:   return MRP_WAYLAND_LAYER_VISIBLE_MASK;
    default:        return 0;
    }
}

static uint32_t get_window_mask(field_t fld)
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
    case VISIBLE:   return MRP_WAYLAND_WINDOW_VISIBLE_MASK;
    case RAISE:     return MRP_WAYLAND_WINDOW_RAISE_MASK;
    case ACTIVE:    return MRP_WAYLAND_WINDOW_ACTIVE_MASK;
    default:        return 0;
    }
}

static bool register_methods(lua_State *L)
{
#define FUNCBRIDGE(n,s,d) { #n, s, n##_bridge, d, &n }
#define FUNCBRIDGE_END    { NULL, NULL, NULL, NULL, NULL }

    static funcbridge_def_t funcbridge_defs[] = {
        FUNCBRIDGE(layer_create  , "ods" , NULL),
        FUNCBRIDGE(layer_request , "oo"  , NULL),
        FUNCBRIDGE(window_request, "oood", NULL),
        FUNCBRIDGE_END
    };

    mrp_funcbridge_t *f;
    funcbridge_def_t *d;
    bool success = true;

    for (d = funcbridge_defs;   d->name;    d++) {
        *(d->ptr) = f = mrp_funcbridge_create_cfunc(L, d->name, d->sign,
                                                    d->func, d->data);
        if (!f) {
            mrp_log_error("failed to register builtin function '%s'", d->name);
            success = false;
        }
    }

    return success;

#undef FUNCBRIDGE_END
#undef FUNCBRIDGE
}
