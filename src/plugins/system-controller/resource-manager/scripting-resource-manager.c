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

#include "scripting-resource-manager.h"
#include "wayland/scripting-wayland.h"
#include "screen.h"
#include "notifier.h"

#define RESOURCE_MANAGER_CLASS   MRP_LUA_CLASS_SIMPLE(resource_manager)

typedef struct scripting_resmgr_s  scripting_resmgr_t;
typedef struct funcbridge_def_s    funcbridge_def_t;

struct scripting_resmgr_s {
    mrp_resmgr_t *resmgr;
    mrp_funcbridge_t *screen_event_handler;
};

struct funcbridge_def_s {
    const char *name;
    const char *sign;
    mrp_funcbridge_cfunc_t func;
    void *data;
    mrp_funcbridge_t **ptr;
};

static int  resmgr_create(lua_State *);
static int  resmgr_getfield(lua_State *);
static int  resmgr_setfield(lua_State *);
static void resmgr_destroy(void *);

static scripting_resmgr_t *resmgr_check(lua_State *, int);

static void screen_event_handler_callback(mrp_resmgr_t *,mrp_resmgr_event_t *);

static bool register_methods(lua_State *);


MRP_LUA_CLASS_DEF_SIMPLE (
    resource_manager,              /* class name */
    scripting_resmgr_t,            /* userdata type */
    resmgr_destroy,                /* userdata destructor */
    MRP_LUA_METHOD_LIST (          /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR    (resmgr_create)
    ),
    MRP_LUA_METHOD_LIST (          /* overrides */
       MRP_LUA_OVERRIDE_CALL         (resmgr_create)
       MRP_LUA_OVERRIDE_GETFIELD     (resmgr_getfield)
       MRP_LUA_OVERRIDE_SETFIELD     (resmgr_setfield)
    )
);


static mrp_funcbridge_t *area_create;
static mrp_funcbridge_t *window_raise;

void mrp_resmgr_scripting_init(lua_State *L)
{
    MRP_ASSERT(L, "invalid argument");

    mrp_lua_create_object_class(L, RESOURCE_MANAGER_CLASS);
    register_methods(L);

    mrp_resmgr_scripting_notifier_init(L);
}


mrp_resmgr_t *mrp_resmgr_scripting_check(lua_State *L, int idx)
{
    scripting_resmgr_t *rm;
    mrp_resmgr_t *resmgr;

    if ((rm = resmgr_check(L, idx)) && (resmgr = rm->resmgr)) {
        MRP_ASSERT(resmgr->scripting_data == (void *)rm,
                   "confused with data structures");
        return resmgr;
    }

    return NULL;
}

mrp_resmgr_t *mrp_resmgr_scripting_unwrap(void *void_rm)
{
    scripting_resmgr_t *rm = (scripting_resmgr_t *)void_rm;

    if (rm && mrp_lua_get_object_classdef(rm) == RESOURCE_MANAGER_CLASS)
        return rm->resmgr;

    return NULL;
}

static int resmgr_create(lua_State *L)
{
    mrp_resmgr_t *resmgr;
    size_t fldnamlen;
    const char *fldnam;
    scripting_resmgr_t *rm;
    int table;
    mrp_funcbridge_t *screen_event_handler = NULL;

    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    rm = (scripting_resmgr_t *)mrp_lua_create_object(L, RESOURCE_MANAGER_CLASS,
                                                     NULL, 1);

    if (!rm)
        luaL_error(L, "failed to create resource manager");

    table = lua_gettop(L);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        switch (mrp_resmgr_scripting_field_name_to_type(fldnam, fldnamlen)) {

        case SCREEN_EVENT_HANDLER:
            screen_event_handler = mrp_funcbridge_create_luafunc(L, -1);
            break;

        default:
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_rawset(L, table);
            break;
        }
    }

    if (!(resmgr = mrp_resmgr_create()))
        luaL_error(L, "can't create resmgr object");

    rm->resmgr = resmgr;
    rm->screen_event_handler = screen_event_handler;

    resmgr->scripting_data = rm;

    mrp_resmgr_notifier_register_event_callback(resmgr,
                                                screen_event_handler_callback);
    lua_settop(L, table);

    MRP_LUA_LEAVE(1);
}

static int resmgr_getfield(lua_State *L)
{
    scripting_resmgr_t *rm;
    const char *fldnam;
    mrp_resmgr_scripting_field_t fld;

    MRP_LUA_ENTER;

    fld = mrp_resmgr_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    rm = resmgr_check(L, 1);

    if (!rm)
        lua_pushnil(L);
    else {
        switch (fld) {

        case AREA_CREATE:
            mrp_funcbridge_push(L, area_create);
            break;

        case WINDOW_RAISE:
            mrp_funcbridge_push(L, window_raise);
            break;

        case SCREEN_EVENT_HANDLER:
            mrp_funcbridge_push(L, rm->screen_event_handler);
            break;

        default:
            lua_pushstring(L, fldnam);
            lua_rawget(L, 1);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  resmgr_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    resmgr_check(L, 1);
    luaL_error(L, "resource manager is read-only");

    MRP_LUA_LEAVE(0);
}

static void resmgr_destroy(void *data)
{
    scripting_resmgr_t *rm = (scripting_resmgr_t *)data;

    MRP_UNUSED(data);
    MRP_UNUSED(rm);

    MRP_LUA_ENTER;

    MRP_LUA_LEAVE_NOARG;
}

static scripting_resmgr_t *resmgr_check(lua_State *L, int idx)
{

    return (scripting_resmgr_t*)mrp_lua_check_object(L, RESOURCE_MANAGER_CLASS,
                                                     idx);
}


static void screen_event_handler_callback(mrp_resmgr_t *resmgr,
                                   mrp_resmgr_event_t *event)
{
    lua_State *L;
    scripting_resmgr_t *rm;
    void *sev;
    mrp_funcbridge_value_t args[4], ret;
    char t;
    bool success;

    MRP_ASSERT(resmgr && event, "invalid argument");

    if (!(rm = resmgr->scripting_data)) {
        mrp_debug("can't deliver resource events to LUA: "
                  "scripting resource manager was not created yet");
        return;
    }

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't deliver resource events: LUA is not initialesed");
        return;
    }

    switch (event->type) {

    case MRP_RESMGR_EVENT_SCREEN:
        sev = mrp_resmgr_scripting_screen_event_create_from_c(L, event);
        break;

    case MRP_RESMGR_EVENT_AUDIO:
        sev = NULL;
        break;

    case MRP_RESMGR_EVENT_INPUT:
        sev = NULL;
        break;

    default:
        sev = NULL;
        break;
    }

    if (!sev) {
        mrp_debug("can't deliver resource events to LUA: "
                  "failed to create scripting event");
        return;
    }

    args[0].pointer = rm;
    args[1].pointer = sev;

    memset(&ret, 0, sizeof(ret));

    success = mrp_funcbridge_call_from_c(L, rm->screen_event_handler, "oo",
                                         args, &t, &ret);
    if (!success) {
        mrp_log_error("failed to call resource_manager.screen_event_handler method "
                      "(%s)", ret.string ? ret.string : "NULL");
        mrp_free((void *)ret.string);
    }
}


static bool area_create_bridge(lua_State *L,
                               void *data,
                               const char *signature,
                               mrp_funcbridge_value_t *args,
                               char *ret_type,
                               mrp_funcbridge_value_t *ret_val)
{
    mrp_wayland_area_t *area;
    mrp_resmgr_t *resmgr;
    const char *zonename;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;

    if (strcmp(signature, "oos")) {
        mrp_log_error("bad signature: expected 'oos' got '%s'",signature);
        return false;
    }

    if (!(resmgr = mrp_resmgr_scripting_unwrap(args[0].pointer))) {
        mrp_log_error("argument 1 is not a 'resource_manager' class object");
        return false;
    }

    if (!(area = mrp_wayland_scripting_area_unwrap(args[1].pointer))) {
        mrp_log_error("argument 2 is not 'area' class object");
        return false;
    }

    zonename = args[2].string;

    mrp_resmgr_screen_area_create(resmgr->screen, area, zonename);

    return true;
}


static bool window_raise_bridge(lua_State *L,
                               void *data,
                               const char *signature,
                               mrp_funcbridge_value_t *args,
                               char *ret_type,
                               mrp_funcbridge_value_t *ret_val)
{
    mrp_resmgr_t *resmgr;
    const char *appid;
    int32_t surfaceid;
    int32_t direction;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;

    if (strcmp(signature, "osdd")) {
        mrp_log_error("bad signature: expected 'osdd' got '%s'", signature);
        return false;
    }

    if (!(resmgr = mrp_resmgr_scripting_unwrap(args[0].pointer))) {
        mrp_log_error("argument 1 is not a 'resource_manager' class object");
        return false;
    }

    if (!(appid = args[1].string)) {
        mrp_log_error("argument 2 is an invalid appid string");
        return false;
    }

    if ((surfaceid = args[2].integer) < 0) {
        mrp_log_error("argument 3 is an invalid surfaceid");
        return false;
    }

    direction = args[3].integer;

    if (direction > 0)
        mrp_screen_resource_raise(resmgr->screen, appid, surfaceid);
    else if (direction < 0)
        mrp_screen_resource_lower(resmgr->screen, appid, surfaceid);

    return true;
}


static bool register_methods(lua_State *L)
{
#define FUNCBRIDGE(n,s,d) { #n, s, n##_bridge, d, &n }
#define FUNCBRIDGE_END    { NULL, NULL, NULL, NULL, NULL }

    static funcbridge_def_t funcbridge_defs[] = {
        FUNCBRIDGE(area_create   , "oos" , NULL),
        FUNCBRIDGE(window_raise  , "osdd", NULL),
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


mrp_resmgr_scripting_field_t
mrp_resmgr_scripting_field_check(lua_State *L,int idx,const char **ret_fldnam)
{
    const char *fldnam;
    size_t fldnamlen;
    mrp_resmgr_scripting_field_t fldtyp;

    if (!(fldnam = lua_tolstring(L, idx, &fldnamlen)))
        fldtyp = 0;
    else
        fldtyp = mrp_resmgr_scripting_field_name_to_type(fldnam, fldnamlen);

    if (ret_fldnam)
        *ret_fldnam = fldnam;

    return fldtyp;
}

mrp_resmgr_scripting_field_t
mrp_resmgr_scripting_field_name_to_type(const char *name, ssize_t len)
{
    if (len < 0)
        len = strlen(name);

    switch (len) {

    case 4:
        switch (name[0]) {
        case 'a':
            if (!strcmp(name, "appid"))
                return APPID;
            if (!strcmp(name, "area"))
                return AREA;
            break;
        case 'n':
            if (!strcmp(name, "name"))
                return NAME;
            break;
        default:
            break;
        }
        break;

    case 5:
        switch (name[0]) {
        case 'a':
            if (!strcmp(name, "AUDIO"))
                return AUDIO;
            break;
        case 'e':
            if (!strcmp(name, "event"))
                return EVENT;
            break;
        case 'i':
            if (!strcmp(name, "input"))
                return INPUT;
            break;
        case 'l':
            if (!strcmp(name, "layer"))
                return LAYER;
            break;
        case 't':
            if (!strcmp(name, "type"))
                return TYPE;
            break;
        default:
            break;
        }
        break;

    case 6:
        if (!strcmp(name, "screen"))
            return SCREEN;
        break;

    case 7:
        switch (name[0]) {
        case 'c':
            if (!strcmp(name, "classes"))
                return CLASSES;
            break;
        case 's':
            if (!strcmp(name, "surface"))
                return SURFACE;
            break;
        default:
            break;
        }
        break;

    case 9:
        if (!strcmp(name, "SHAREABLE"))
            return SHAREABLE;
        break;

    case 10:
        if (!strcmp(name, "attributes"))
            return ATTRIBUTES;
        break;

    case 11:
        if (!strcmp(name, "area_create"))
            return AREA_CREATE;
        break;

    case 12:
        if (!strcmp(name, "window_raise"))
            return WINDOW_RAISE;
        break;

    case 20:
        if (!strcmp(name, "screen_event_handler"))
            return SCREEN_EVENT_HANDLER;
        break;

    default:
        break;
    }

    return 0;
}