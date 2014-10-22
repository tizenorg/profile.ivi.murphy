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
#include "window.h"
#include "layer.h"
#include "output.h"
#include "area.h"
#include "window-manager.h"

#ifdef WESTON_ICO_PLUGINS
#include "ico-window-manager.h"
#else
#include "glm-window-manager.h"
#endif

#define WINDOW_MANAGER_CLASS   MRP_LUA_CLASS_SIMPLE(window_manager)

typedef struct scripting_winmgr_s  scripting_winmgr_t;
typedef struct funcbridge_def_s    funcbridge_def_t;
typedef struct layer_def_s         layer_def_t;
typedef struct request_def_s       request_def_t;

struct scripting_winmgr_s {
    mrp_wayland_t *wl;
    const char *name;
    const char *display;
    mrp_funcbridge_t *manager_update;
    mrp_funcbridge_t *output_update;
    mrp_funcbridge_t *layer_update;
    mrp_funcbridge_t *window_update;
};

struct funcbridge_def_s {
    const char *name;
    const char *sign;
    mrp_funcbridge_cfunc_t func;
    void *data;
    mrp_funcbridge_t **ptr;
};


struct layer_def_s {
    int32_t id;
    const char *name;
    int32_t type;
    const char *output;
};

struct request_def_s {
    const char *name;
    int offset;
    int mask;
    mrp_wayland_json_copy_func_t copy;
};


static int  window_manager_create(lua_State *);
static int  window_manager_connect(lua_State *);
static int  window_manager_disconnect(lua_State *L);
static int  window_manager_canonical_name(lua_State *);
static int  window_manager_getfield(lua_State *);
static int  window_manager_setfield(lua_State *);
static void window_manager_destroy(void *);

static scripting_winmgr_t *window_manager_check(lua_State *, int);

static layer_def_t *layer_def_check(lua_State *, int);
static void layer_def_free(layer_def_t *);


static bool manager_request_bridge(lua_State *, void *,
                                   const char *, mrp_funcbridge_value_t *,
                                   char *, mrp_funcbridge_value_t *);
static void manager_update_callback(mrp_wayland_t *,
                                    mrp_wayland_window_manager_operation_t,
                                    mrp_wayland_window_manager_t *);
static void window_hint_callback(mrp_wayland_t *,
                                 mrp_wayland_window_operation_t,
                                 mrp_wayland_window_update_t *);

static bool window_request_bridge(lua_State *, void *,
                                  const char *, mrp_funcbridge_value_t *,
                                  char *, mrp_funcbridge_value_t *);
static void window_update_callback(mrp_wayland_t *,
                                   mrp_wayland_window_operation_t,
                                   mrp_wayland_window_update_mask_t,
                                   mrp_wayland_window_t *);

static bool output_request_bridge(lua_State *, void *,
                                  const char *, mrp_funcbridge_value_t *,
                                  char *, mrp_funcbridge_value_t *);
static void output_update_callback(mrp_wayland_t *,
                                   mrp_wayland_output_operation_t,
                                   mrp_wayland_output_update_mask_t,
                                   mrp_wayland_output_t *);

static bool area_create_bridge(lua_State *, void *,
                               const char *, mrp_funcbridge_value_t *,
                               char *, mrp_funcbridge_value_t *);

static bool layer_request_bridge(lua_State *, void *,
                                 const char *, mrp_funcbridge_value_t *,
                                 char *, mrp_funcbridge_value_t *);
static void layer_update_callback(mrp_wayland_t *,
                                  mrp_wayland_layer_operation_t,
                                  mrp_wayland_layer_update_mask_t,
                                  mrp_wayland_layer_t *);

static bool buffer_request_bridge(lua_State *, void *,
                                  const char *, mrp_funcbridge_value_t *,
                                  char *, mrp_funcbridge_value_t *);


static uint32_t copy_json_fields(mrp_wayland_t *, mrp_json_t *,
                                 request_def_t *, void *);
static bool register_methods(lua_State *);


MRP_LUA_CLASS_DEF_SIMPLE (
    window_manager,                /* class name */
    scripting_winmgr_t,            /* userdata type */
    window_manager_destroy,        /* userdata destructor */
    MRP_LUA_METHOD_LIST (          /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR    (window_manager_create)
       MRP_LUA_METHOD(connect,        window_manager_connect)
       MRP_LUA_METHOD(disconnect,     window_manager_disconnect)
       MRP_LUA_METHOD(canonical_name, window_manager_canonical_name)
    ),
    MRP_LUA_METHOD_LIST (          /* overrides */
       MRP_LUA_OVERRIDE_CALL         (window_manager_create)
       MRP_LUA_OVERRIDE_GETFIELD     (window_manager_getfield)
       MRP_LUA_OVERRIDE_SETFIELD     (window_manager_setfield)
    )
);

static mrp_funcbridge_t *manager_request;
static mrp_funcbridge_t *output_request;
static mrp_funcbridge_t *area_create;
static mrp_funcbridge_t *layer_request;
static mrp_funcbridge_t *window_request;
static mrp_funcbridge_t *buffer_request;


void mrp_wayland_scripting_window_manager_init(lua_State *L)
{
    MRP_ASSERT(L, "invalid argument");

    mrp_lua_create_object_class(L, WINDOW_MANAGER_CLASS);
    register_methods(L);
}

mrp_wayland_t *mrp_wayland_scripting_window_manager_check(lua_State *L,int idx)
{
    scripting_winmgr_t *wmgr;
    mrp_wayland_t *wl;

    if ((wmgr = window_manager_check(L, idx)) && (wl = wmgr->wl)) {
        MRP_ASSERT(wl->scripting_window_data == (void *)wmgr,
                   "confused with data structures");
        return wl;
    }

    return NULL;
}

mrp_wayland_t *mrp_wayland_scripting_window_manager_unwrap(void *void_wmgr)
{
    scripting_winmgr_t *wmgr = (scripting_winmgr_t *)void_wmgr;

    if (wmgr && mrp_lua_get_object_classdef(wmgr) == WINDOW_MANAGER_CLASS)
        return wmgr->wl;

    return NULL;
}

static int window_manager_create(lua_State *L)
{
    mrp_context_t *ctx;
    mrp_wayland_t *wl;
    size_t fldnamlen;
    const char *fldnam;
    scripting_winmgr_t *winmgr;
    mrp_wayland_layer_update_t lu;
    char *name;
    const char *display = NULL;
    layer_def_t *layers = NULL;
    mrp_funcbridge_t *manager_update = NULL;
    mrp_funcbridge_t *output_update = NULL;
    mrp_funcbridge_t *layer_update = NULL;
    mrp_funcbridge_t *window_update = NULL;
    char buf[256];
    layer_def_t *l;
    int table;

    MRP_LUA_ENTER;

    ctx = mrp_lua_get_murphy_context();

    MRP_ASSERT(ctx && ctx->ml, "invalid app.context or missing mainloop");

    luaL_checktype(L, 2, LUA_TTABLE);

    winmgr = (scripting_winmgr_t*)mrp_lua_create_object(L,WINDOW_MANAGER_CLASS,
                                                        NULL, 0);
    if (!winmgr)
        luaL_error(L, "can't create window manager on display '%s'", display);

    table = lua_gettop(L);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        switch (mrp_wayland_scripting_field_name_to_type(fldnam, fldnamlen)) {

        case DISPLAY:
            display = luaL_checkstring(L, -1);
            break;

        case LAYERS:
            layers = layer_def_check(L, -1);
            break;

        case MANAGER_UPDATE:
            manager_update = mrp_funcbridge_create_luafunc(L, -1);
            break;

        case OUTPUT_UPDATE:
            output_update = mrp_funcbridge_create_luafunc(L, -1);
            break;

        case LAYER_UPDATE:
            layer_update = mrp_funcbridge_create_luafunc(L, -1);
            break;

        case WINDOW_UPDATE:
            window_update = mrp_funcbridge_create_luafunc(L, -1);
            break;

        default:
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_rawset(L, table);
            break;
        }
    }

    if (!display)
        display = getenv("WAYLAND_DISPLAY");

    if (!display)
        display = "wayland-0";

    name = mrp_wayland_scripting_canonical_name(display, buf, sizeof(buf));

    if (!(wl = mrp_wayland_create(display, ctx->ml)))
        luaL_error(L, "can't create wayland object");

    mrp_lua_set_object_name(L, WINDOW_MANAGER_CLASS, name);

    winmgr->wl = wl;
    winmgr->name = mrp_strdup(name);
    winmgr->display = mrp_strdup(display);
    winmgr->manager_update = manager_update;
    winmgr->output_update = output_update;
    winmgr->layer_update = layer_update;
    winmgr->window_update = window_update;

    mrp_wayland_output_register(wl);
#ifdef WESTON_ICO_PLUGINS
    mrp_ico_window_manager_register(wl);
#else
    mrp_glm_window_manager_register(wl);
#endif

    mrp_wayland_register_window_manager_update_callback(wl,
                                                   manager_update_callback);
    mrp_wayland_register_output_update_callback(wl, output_update_callback);
    mrp_wayland_register_layer_update_callback(wl, layer_update_callback);
    mrp_wayland_register_window_update_callback(wl, window_update_callback);
    mrp_wayland_register_window_hint_callback(wl, window_hint_callback);

    mrp_wayland_set_scripting_window_data(wl, winmgr);

    mrp_wayland_create_scripting_windows(wl, true);
    mrp_wayland_create_scripting_outputs(wl, true);
    mrp_wayland_create_scripting_areas(wl, true);
    mrp_wayland_create_scripting_layers(wl, true);

    if (layers) {
        memset(&lu, 0, sizeof(lu));
        lu.mask = MRP_WAYLAND_LAYER_LAYERID_MASK    |
                  MRP_WAYLAND_LAYER_NAME_MASK       |
                  MRP_WAYLAND_LAYER_TYPE_MASK       |
                  MRP_WAYLAND_LAYER_OUTPUTNAME_MASK ;

        for (l = layers;  l->name;  l++) {
            lu.layerid = l->id;
            lu.name = l->name;
            lu.type = l->type;
            lu.outputname = (char *)l->output;
            mrp_wayland_layer_create(wl, &lu);
        }

        layer_def_free(layers);
    }

    lua_settop(L, table);

    MRP_LUA_LEAVE(1);
}

static int window_manager_connect(lua_State *L)
{
    scripting_winmgr_t *wmgr;
    bool success;

    MRP_LUA_ENTER;

    wmgr = window_manager_check(L, 1);

    if (wmgr->wl != NULL)
        success = mrp_wayland_connect(wmgr->wl);
    else
        success = false;

    lua_pushboolean(L, success);

    MRP_LUA_LEAVE(1);
}

static int window_manager_disconnect(lua_State *L)
{
    scripting_winmgr_t *wmgr;
    bool success = false;

    MRP_LUA_ENTER;

    wmgr = window_manager_check(L, 1);

    /* destroy all screen and input resources */

    /* disconnect wayland */

    success = mrp_wayland_disconnect(wmgr->wl);

    lua_pushboolean(L, success);

    MRP_LUA_LEAVE(1);
}

static int  window_manager_canonical_name(lua_State *L)
{
    const char *name;
    char *canonical;
    char buf[2048];

    MRP_LUA_ENTER;

    *(canonical = buf) = 0;

    if ((name = luaL_checkstring(L, 2)))
        canonical = mrp_wayland_scripting_canonical_name(name,buf,sizeof(buf));

    lua_pushstring(L, canonical);

    MRP_LUA_LEAVE(1);
}

static int window_manager_getfield(lua_State *L)
{
    scripting_winmgr_t *wmgr;
    mrp_wayland_t *wl;
    const char *fldnam;
    mrp_wayland_scripting_field_t fld;
    uint32_t mask;

    MRP_LUA_ENTER;

    fld = mrp_wayland_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    wmgr = window_manager_check(L, 1);

    if (!wmgr || !(wl = wmgr->wl))
        lua_pushnil(L);
    else {
        switch (fld) {

        case DISPLAY:
            lua_pushstring(L, wmgr->display);
            break;

        case MANAGER_REQUEST:
            mrp_funcbridge_push(L, manager_request);
            break;

        case MANAGER_UPDATE:
            mrp_funcbridge_push(L, wmgr->manager_update);
            break;

        case OUTPUT_REQUEST:
            mrp_funcbridge_push(L, output_request);
            break;

        case OUTPUT_UPDATE:
            mrp_funcbridge_push(L, wmgr->output_update);
            break;

        case AREA_CREATE:
            mrp_funcbridge_push(L, area_create);
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

        case BUFFER_REQUEST:
            mrp_funcbridge_push(L, buffer_request);
            break;

        case PASSTHROUGH_WINDOW_REQUEST:
            mask = (wl->wm ? wl->wm->passthrough.window_request : 0);
            goto push_window_mask;

        case PASSTHROUGH_WINDOW_UPDATE:
            mask = (wl->wm ? wl->wm->passthrough.window_update : 0);
            goto push_window_mask;

        case PASSTHROUGH_LAYER_REQUEST:
            mask = (wl->wm ? wl->wm->passthrough.layer_request : 0);
            goto push_layer_mask;

        case PASSTHROUGH_LAYER_UPDATE:
            mask = (wl->wm ? wl->wm->passthrough.layer_update : 0);
            goto push_layer_mask;

        push_window_mask:
            if (!mrp_wayland_scripting_window_mask_create_from_c(L, mask))
                lua_pushnil(L);
            break;

        push_layer_mask:
            if (!mrp_wayland_scripting_layer_mask_create_from_c(L, mask))
                lua_pushnil(L);
            break;

        default:
            lua_pushstring(L, fldnam);
            lua_rawget(L, 1);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  window_manager_setfield(lua_State *L)
{
    scripting_winmgr_t *wmgr;

    MRP_LUA_ENTER;

    wmgr = window_manager_check(L, 1);
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

static scripting_winmgr_t *window_manager_check(lua_State *L, int idx)
{

    return (scripting_winmgr_t *)mrp_lua_check_object(L, WINDOW_MANAGER_CLASS,
                                                      idx);
}

static layer_def_t *layer_def_check(lua_State *L, int t)
{
    size_t tlen, dlen;
    size_t size;
    layer_def_t *layers, *l;
    size_t i, j;

    t = (t < 0) ? lua_gettop(L) + t + 1 : t;

    luaL_checktype(L, t, LUA_TTABLE);
    tlen  = lua_objlen(L, t);
    size = sizeof(layer_def_t) * (tlen + 1);

    if (!(layers = mrp_allocz(size)))
        luaL_error(L, "can't allocate %d byte long memory", size);
    else {
        for (i = 0;  i < tlen;  i++) {
            l = layers + i;

            lua_pushinteger(L, (int)(i+1));
            lua_gettable(L, t);

            if (!lua_istable(L, -1))
                goto error;

            dlen = lua_objlen(L, -1);

            for (j = 0;  j < dlen;  j++) {
                lua_pushnumber(L, (int)(j+1));
                lua_gettable(L, -2);

                switch (j) {
                case 0:   l->id = lua_tointeger(L, -1);                 break;
                case 1:   l->name = mrp_strdup(lua_tostring(L, -1));    break;
                case 2:   l->type = lua_tointeger(L, -1);               break;
                case 3:   l->output = mrp_strdup(lua_tostring(L, -1));  break;
                default:  goto error;
                }

                lua_pop(L, 1);
            }

            lua_pop(L, 1);

            if (!l->name || l->id < 0 || l->id > 0x10000 || !l->output)
                goto error;
        }
    }

    return layers;

 error:
    layer_def_free(layers);
    luaL_argerror(L, i+1, "malformed layer definition");
    return NULL;
}

static void layer_def_free(layer_def_t *layers)
{
    layer_def_t *l;

    if (layers) {
        for (l = layers;  l->name;  l++) {
            mrp_free((void *)l->name);
            mrp_free((void *)l->output);
        }
        mrp_free((void *)layers);
    }
}

static bool manager_request_bridge(lua_State *L,
                                   void *data,
                                   const char *signature,
                                   mrp_funcbridge_value_t *args,
                                   char *ret_type,
                                   mrp_funcbridge_value_t *ret_val)
{
    mrp_wayland_t *wl;
    mrp_wayland_window_manager_t *wm;
    mrp_json_t *json;
    const char *key;
    mrp_json_t *val;
    mrp_json_iter_t it;
    int32_t mask;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;

    if (strcmp(signature, "oo")) {
        mrp_log_error("system-controller: bad signature: "
                      "expected 'oo' got '%s'", signature);
        return false;
    }

    if (!(wl = mrp_wayland_scripting_window_manager_unwrap(args[0].pointer))) {
        mrp_log_error("system-controller: argument 1 is not a "
                      "'window_manager' class object");
        return false;
    }

    if (!(wm = wl->wm)) {
        mrp_log_error("system-controller: 'window_manager' is not initilized");
        return false;
    }

    if (!(json = mrp_json_lua_unwrap(args[1].pointer))) {
        mrp_log_error("system-controller: argument 2 is not a "
                      "'JSON' class object");
        return false;
    }

    mrp_json_foreach_member(json, key,val, it) {
        switch (mrp_wayland_scripting_field_name_to_type(key, -1)) {

        case PASSTHROUGH_WINDOW_REQUEST:
            if (!mrp_wayland_json_integer_copy(wl, &mask, val, 1))
                mrp_debug("'%s' field has invalid value", key);
            else {
                mrp_debug("set passthrough.window_request to 0x%x", mask);
                wm->passthrough.window_request = mask;
            }
            break;

        case PASSTHROUGH_WINDOW_UPDATE:
            if (!mrp_wayland_json_integer_copy(wl, &mask, val, 1))
                mrp_debug("'%s' field has invalid value", key);
            else {
                mrp_debug("set passthrough.window_update to 0x%x", mask);
                wm->passthrough.window_update = mask;
            }
            break;

        case PASSTHROUGH_LAYER_REQUEST:
            if (!mrp_wayland_json_integer_copy(wl, &mask, val, 1))
                mrp_debug("'%s' field has invalid value", key);
            else {
                mrp_debug("set passthrough.layer_request to 0x%x", mask);
                wm->passthrough.layer_request = mask;
            }
            break;

        case PASSTHROUGH_LAYER_UPDATE:
            if (!mrp_wayland_json_integer_copy(wl, &mask, val, 1))
                mrp_debug("'%s' field has invalid value", key);
            else {
                mrp_debug("set passthrough.layer_update to 0x%x", mask);
                wm->passthrough.layer_update = mask;
            }
            break;

        default:
            mrp_debug("ignoring JSON field '%s'", key);
            break;
        }
    }

    mrp_json_unref(json);

    return true;
}

static void manager_update_callback(mrp_wayland_t *wl,
                                   mrp_wayland_window_manager_operation_t oper,
                                   mrp_wayland_window_manager_t *wm)
{
    lua_State *L;
    scripting_winmgr_t *winmgr;
    mrp_funcbridge_value_t args[4], ret;
    char t;
    bool success;

    MRP_ASSERT(wl && wm, "invalid argument");

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("sysem-controller: can't update manager: "
                      "LUA is not initialesed");
        return;
    }

    if (!(winmgr = (scripting_winmgr_t *)wl->scripting_window_data)) {
        mrp_log_error("system-controller: window manager "
                      "scripting is not initialized");
        return;
    }

    MRP_ASSERT(wl == winmgr->wl, "confused with data structures");

    args[0].pointer = winmgr;
    args[1].integer = oper;

    memset(&ret, 0, sizeof(ret));

    success = mrp_funcbridge_call_from_c(L, winmgr->manager_update, "od",
                                         args, &t, &ret);
    if (!success) {
        mrp_log_error("failed to call window_manager.%s.manager_update method "
                      "(%s)", winmgr->name, ret.string ? ret.string : "NULL");
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
#define TYPE(t)  mrp_wayland_json_ ## t ## _copy

    static request_def_t   fields[] = {
        { "surface", FIELD(surfaceid), MASK(SURFACEID), TYPE(integer)  },
        { "layer"  , FIELD(layer)    , MASK(LAYER)    , TYPE(layer)    },
        { "node"   , FIELD(nodeid)   , MASK(NODEID)   , TYPE(integer)  },
        { "pos_x"  , FIELD(x)        , MASK(X)        , TYPE(integer)  },
        { "pos_y"  , FIELD(y)        , MASK(Y)        , TYPE(integer)  },
        { "width"  , FIELD(width)    , MASK(WIDTH)    , TYPE(integer)  },
        { "height" , FIELD(height)   , MASK(HEIGHT)   , TYPE(integer)  },
        { "opacity", FIELD(opacity)  , MASK(OPACITY)  , TYPE(floating) },
        { "raise"  , FIELD(raise)    , MASK(RAISE)    , TYPE(integer)  },
        { "visible", FIELD(visible)  , MASK(VISIBLE)  , TYPE(integer)  },
        { "active" , FIELD(active)   , MASK(ACTIVE)   , TYPE(integer)  },
        { "mapped" , FIELD(mapped)   , MASK(MAPPED)   , TYPE(integer)  },
        { "area"   , FIELD(area)     , MASK(AREA)     , TYPE(area)     },
        {   NULL   ,        0        ,      0         ,      NULL      }
    };

#undef FIELD
#undef MASK
#undef TYPE

    mrp_wayland_t *wl;
    mrp_wayland_animation_t *anims;
    mrp_wayland_window_update_t u;
    uint32_t framerate;
    mrp_json_t *json;
    bool success;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;
    json = NULL;

    if (strcmp(signature, "oood")) {
        mrp_log_error("system-controller: bad signature: "
                      "expected 'oood' got '%s'",signature);
        success = false;
        goto out;
    }

    if (!(wl = mrp_wayland_scripting_window_manager_unwrap(args[0].pointer))) {
        mrp_log_error("system-controller: argument 1 is not a "
                      "'window_manager' class object");
        success = false;
        goto out;
    }

    if (!(json = mrp_json_lua_unwrap(args[1].pointer))) {
        mrp_log_error("system-controller: argument 2 is not a "
                      "'JSON' class object");
        success = false;
        goto out;
    }

    if (!(anims = mrp_wayland_scripting_animation_unwrap(args[2].pointer))) {
        mrp_log_error("system-controller: argument 3 is not an "
                      "'animation' class object");
        success = false;
        goto out;
    }

    if ((framerate = args[3].integer) > MRP_WAYLAND_FRAMERATE_MAX) {
        mrp_log_error("system-controller: argument 3 is not valid framerate "
                      "(out of range 0-%d)", MRP_WAYLAND_FRAMERATE_MAX);
        success = false;
        goto out;
    }


    memset(&u, 0, sizeof(u));
    u.mask = copy_json_fields(wl, json, fields, &u);

    mrp_wayland_window_request(wl, &u, anims, framerate);

    success = true;

 out:
    mrp_json_unref(json);

    return success;
}


static void window_update_callback(mrp_wayland_t *wl,
                                   mrp_wayland_window_operation_t oper,
                                   mrp_wayland_window_update_mask_t mask,
                                   mrp_wayland_window_t *win)
{
    lua_State *L;
    scripting_winmgr_t *winmgr;
    mrp_funcbridge_value_t args[4], ret;
    int top;
    char t;
    bool success;

    MRP_ASSERT(wl && win, "invalid argument");

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("system-controller: can't update window %u: "
                      "LUA is not initialesed", win->surfaceid);
        return;
    }

    if (!(winmgr = (scripting_winmgr_t *)wl->scripting_window_data)) {
        mrp_log_error("system-controller: window manager scripting is "
                      "not initialized");
        return;
    }

    MRP_ASSERT(wl == winmgr->wl, "confused with data structures");

    if (!win->scripting_data) {
        mrp_log_error("system-controller: no scripting data for "
                      "window %d", win->surfaceid);
        return;
    }

    top = lua_gettop(L);

    args[0].pointer = winmgr;
    args[1].integer = oper;
    args[2].pointer = win->scripting_data;
    args[3].pointer = mrp_wayland_scripting_window_mask_create_from_c(L, mask);

    memset(&ret, 0, sizeof(ret));

    success = mrp_funcbridge_call_from_c(L, winmgr->window_update, "odoo",
                                         args, &t, &ret);
    if (!success) {
        mrp_log_error("failed to call window_manager.%s.window_update method "
                      "(%s)", winmgr->name, ret.string ? ret.string : "NULL");
        mrp_free((void *)ret.string);
    }

    lua_settop(L, top);
}

static void window_hint_callback(mrp_wayland_t *wl,
                                 mrp_wayland_window_operation_t oper,
                                 mrp_wayland_window_update_t *hint)
{
    lua_State *L;
    scripting_winmgr_t *winmgr;
    void *wh;
    mrp_funcbridge_value_t args[4], ret;
    int top;
    char t;
    bool success;

    MRP_ASSERT(wl && hint, "invalid argument");

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("system-controller: can't hint window %u: "
                      "LUA is not initialesed", hint->surfaceid);
        return;
    }

    if (!(winmgr = (scripting_winmgr_t *)wl->scripting_window_data)) {
        mrp_log_error("system-controller: window manager scripting is "
                      "not initialized");
        return;
    }

    MRP_ASSERT(wl == winmgr->wl, "confused with data structures");

    top = lua_gettop(L);

    if (!(wh = mrp_wayland_scripting_window_hint_create_from_c(L, hint)))
        return;

    args[0].pointer = winmgr;
    args[1].integer = oper;
    args[2].pointer = wh;
    args[3].pointer = mrp_wayland_scripting_window_mask_create_from_c(L,
                                                                   hint->mask);
    memset(&ret, 0, sizeof(ret));

    success = mrp_funcbridge_call_from_c(L, winmgr->window_update, "odoo",
                                         args, &t, &ret);
    if (!success) {
        mrp_log_error("failed to call window_manager.%s.window_update method "
                      "(%s)", winmgr->name, ret.string ? ret.string : "NULL");
        mrp_free((void *)ret.string);
    }

    lua_settop(L, top);
}

static bool output_request_bridge(lua_State *L,
                                  void *data,
                                  const char *signature,
                                  mrp_funcbridge_value_t *args,
                                  char *ret_type,
                                  mrp_funcbridge_value_t *ret_val)
{
#define FIELD(f) MRP_OFFSET(mrp_wayland_output_update_t, f)
#define MASK(m)  MRP_WAYLAND_OUTPUT_ ## m ## _MASK
#define TYPE(t)  mrp_wayland_json_ ## t ## _copy

    static request_def_t   fields[] = {
        { "index" , FIELD(index)     , MASK(INDEX)   , TYPE(integer) },
        { "id"    , FIELD(outputid)  , MASK(OUTPUTID), TYPE(integer) },
        { "name"  , FIELD(outputname), MASK(NAME)    , TYPE(string)  },
        {  NULL   ,        0         ,      0        ,     NULL      }
    };

#undef FIELD
#undef MASK
#undef TYPE

    mrp_wayland_t *wl;
    mrp_wayland_output_update_t u;
    mrp_json_t *json;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;

    if (strcmp(signature, "oo")) {
        mrp_log_error("bad signature: expected 'oo' got '%s'",signature);
        return false;
    }

    if (!(wl = mrp_wayland_scripting_window_manager_unwrap(args[0].pointer))) {
        mrp_log_error("argument 1 is not a 'window_manager' class object");
        return false;
    }

    if (!(json = mrp_json_lua_unwrap(args[1].pointer))) {
        mrp_log_error("argument 2 is not a 'JSON' class object");
        return false;
    }


    memset(&u, 0, sizeof(u));
    u.mask = copy_json_fields(wl, json, fields, &u);

    mrp_wayland_output_request(wl, &u);

    mrp_json_unref(json);

    return true;
}

static void output_update_callback(mrp_wayland_t *wl,
                                  mrp_wayland_output_operation_t oper,
                                  mrp_wayland_output_update_mask_t mask,
                                  mrp_wayland_output_t *out)
{
    lua_State *L;
    scripting_winmgr_t *winmgr;
    mrp_funcbridge_value_t args[4], ret;
    int top;
    char t;
    bool success;

    MRP_ASSERT(wl && out, "invalid argument");

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't update output %d '%s': LUA is not initialesed",
                      out->outputid, out->outputname);
        return;
    }

    if (!(winmgr = (scripting_winmgr_t *)wl->scripting_window_data)) {
        mrp_log_error("window manager scripting is not initialized");
        return;
    }

    MRP_ASSERT(wl == winmgr->wl, "confused with data structures");

    if (!out->scripting_data) {
        mrp_log_error("no scripting data for output %d '%s'",
                      out->outputid, out->outputname);
        return;
    }

    top = lua_gettop(L);

    args[0].pointer = winmgr;
    args[1].integer = oper;
    args[2].pointer = out->scripting_data;
    args[3].pointer = mrp_wayland_scripting_output_mask_create_from_c(L, mask);

    memset(&ret, 0, sizeof(ret));

    success = mrp_funcbridge_call_from_c(L, winmgr->output_update, "odoo",
                                         args, &t, &ret);
    if (!success) {
        mrp_log_error("failed to call window_manager.%s.output_update method "
                      "(%s)", winmgr->name, ret.string ? ret.string : "NULL");
        mrp_free((void *)ret.string);
    }

    lua_settop(L, top);
}

static bool area_create_bridge(lua_State *L,
                               void *data,
                               const char *signature,
                               mrp_funcbridge_value_t *args,
                               char *ret_type,
                               mrp_funcbridge_value_t *ret_val)
{
#define FIELD(f) MRP_OFFSET(mrp_wayland_area_update_t, f)
#define MASK(m)  MRP_WAYLAND_AREA_ ## m ## _MASK
#define TYPE(t)  mrp_wayland_json_ ## t ## _copy

    static request_def_t   fields[] = {
        { "id"        , FIELD(areaid)   , MASK(AREAID)   , TYPE(integer) },
        { "name"      , FIELD(name)     , MASK(NAME)     , TYPE(string)  },
        { "output"    , FIELD(output)   , MASK(OUTPUT)   , TYPE(output)  },
        { "pos_x"     , FIELD(x)        , MASK(X)        , TYPE(integer) },
        { "pos_y"     , FIELD(y)        , MASK(Y)        , TYPE(integer) },
        { "width"     , FIELD(width)    , MASK(WIDTH)    , TYPE(integer) },
        { "height"    , FIELD(height)   , MASK(HEIGHT)   , TYPE(integer) },
        { "keep_ratio", FIELD(keepratio), MASK(KEEPRATIO), TYPE(boolean) },
        { "align"     , FIELD(align)    , MASK(ALIGN)    , TYPE(align)   },
        {  NULL       ,       0         ,       0        ,     NULL      }
    };

#undef FIELD
#undef MASK
#undef TYPE

    mrp_wayland_t *wl;
    mrp_wayland_area_update_t u;
    mrp_json_t *json;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;


    if (strcmp(signature, "oo")) {
        mrp_log_error("bad signature: expected 'oo' got '%s'",signature);
        return false;
    }

    if (!(wl = mrp_wayland_scripting_window_manager_unwrap(args[0].pointer))) {
        mrp_log_error("argument 1 is not a 'window_manager' class object");
        return false;
    }

    if (!(json = mrp_json_lua_unwrap(args[1].pointer))) {
        mrp_log_error("argument 2 is not a 'JSON' class object");
        return false;
    }

    memset(&u, 0, sizeof(u));
    u.mask = copy_json_fields(wl, json, fields, &u);

    mrp_wayland_area_create(wl, &u);

    mrp_json_unref(json);

    return true;
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
#define TYPE(t)  mrp_wayland_json_ ## t ## _copy

    static request_def_t   fields[] = {
        { "layer"  , FIELD(layerid)  , MASK(LAYERID), TYPE(integer) },
        { "visible", FIELD(visible)  , MASK(VISIBLE), TYPE(integer) },
        {   NULL   ,        0        ,      0       ,      NULL     }
    };

#undef FIELD
#undef MASK
#undef TYPE

    mrp_wayland_t *wl;
    mrp_wayland_layer_update_t u;
    mrp_json_t *json;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;

    if (strcmp(signature, "oo")) {
        mrp_log_error("bad signature: expected 'oo' got '%s'",signature);
        return false;
    }

    if (!(wl = mrp_wayland_scripting_window_manager_unwrap(args[0].pointer))) {
        mrp_log_error("argument 1 is not a 'window_manager' class object");
        return false;
    }

    if (!(json = mrp_json_lua_unwrap(args[1].pointer))) {
        mrp_log_error("argument 2 is not a 'JSON' class object");
        return false;
    }

    memset(&u, 0, sizeof(u));
    u.mask = copy_json_fields(wl, json, fields, &u);

    mrp_wayland_layer_request(wl, &u);

    mrp_json_unref(json);

    return true;
}

static void layer_update_callback(mrp_wayland_t *wl,
                                  mrp_wayland_layer_operation_t oper,
                                  mrp_wayland_layer_update_mask_t mask,
                                  mrp_wayland_layer_t *layer)
{
    lua_State *L;
    scripting_winmgr_t *winmgr;
    mrp_funcbridge_value_t args[4], ret;
    int top;
    char t;
    bool success;

    MRP_ASSERT(wl && layer, "invalid argument");

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't update layer %u: LUA is not initialesed",
                      layer->layerid);
        return;
    }

    if (!(winmgr = (scripting_winmgr_t *)wl->scripting_window_data)) {
        mrp_log_error("window manager scripting is not initialized");
        return;
    }

    MRP_ASSERT(wl == winmgr->wl, "confused with data structures");

    if (!layer->scripting_data) {
        mrp_log_error("no scripting data for layer %d", layer->layerid);
        return;
    }

    top = lua_gettop(L);

    args[0].pointer = winmgr;
    args[1].integer = oper;
    args[2].pointer = layer->scripting_data;
    args[3].pointer = mrp_wayland_scripting_layer_mask_create_from_c(L, mask);

    memset(&ret, 0, sizeof(ret));

    success = mrp_funcbridge_call_from_c(L, winmgr->layer_update, "odoo",
                                         args, &t, &ret);
    if (!success) {
        mrp_log_error("failed to call window_manager.%s.output_update method "
                      "(%s)", winmgr->name, ret.string ? ret.string : "NULL");
        mrp_free((void *)ret.string);
    }

    lua_settop(L, top);
}


static bool buffer_request_bridge(lua_State *L,
                                  void *data,
                                  const char *signature,
                                  mrp_funcbridge_value_t *args,
                                  char *ret_type,
                                  mrp_funcbridge_value_t *ret_val)
{
    mrp_wayland_t *wl;
    mrp_wayland_window_manager_t *wm;
    const char *shmname;
    uint32_t bufsize;
    uint32_t bufnum;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;

    if (strcmp(signature, "osdd")) {
        mrp_log_error("system-controller: bad signature: "
                      "expected 'osdd' got '%s'",signature);
        return false;
    }

    if (!(wl = mrp_wayland_scripting_window_manager_unwrap(args[0].pointer))) {
        mrp_log_error("system-controller: argument 1 is not a "
                      "'window_manager' class object");
        return false;
    }

    if (!(wm = wl->wm)) {
        mrp_debug("ignoring buffer request: window manager is down");
        return false;
    }

    shmname = args[1].string;
    bufsize = args[2].integer;
    bufnum  = args[3].integer;

    if (wm->buffer_request)
        wm->buffer_request(wm, shmname, bufsize, bufnum);

    return true;
}


static uint32_t copy_json_fields(mrp_wayland_t *wl,
                                 mrp_json_t *json,
                                 request_def_t *fields,
                                 void *copy)
{
    const char *key;
    mrp_json_t *val;
    mrp_json_iter_t it;
    request_def_t *f;
    uint32_t m, mask;
    bool found;

    mask = 0;

    mrp_json_foreach_member(json, key,val, it) {
        for (f = fields, found = false;   f->name;   f++) {
            if (!strcmp(key, f->name)) {
                found = true;
                if ((m = f->copy(wl, copy + f->offset, val, f->mask)))
                    mask |= m;
                else
                    mrp_debug("'%s' field has invalid value", key);
                break;
            }
        }
        if (!found)
            mrp_debug("ignoring JSON field '%s'", key);
    }

    return mask;
}


static bool register_methods(lua_State *L)
{
#define FUNCBRIDGE(n,s,d) { #n, s, n##_bridge, d, &n }
#define FUNCBRIDGE_END    { NULL, NULL, NULL, NULL, NULL }

    static funcbridge_def_t funcbridge_defs[] = {
        FUNCBRIDGE(manager_request, "oo"  , NULL),
        FUNCBRIDGE(output_request , "oo"  , NULL),
        FUNCBRIDGE(area_create    , "oo"  , NULL),
        FUNCBRIDGE(layer_request  , "oo"  , NULL),
        FUNCBRIDGE(window_request , "oood", NULL),
        FUNCBRIDGE(buffer_request , "osdd", NULL),
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
