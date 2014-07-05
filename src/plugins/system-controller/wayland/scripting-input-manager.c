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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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
#include "input-manager.h"
#include "ico-input-manager.h"

#define INPUT_MANAGER_CLASS   MRP_LUA_CLASS_SIMPLE(input_manager)

typedef struct scripting_inpmgr_s  scripting_inpmgr_t;
typedef struct funcbridge_def_s    funcbridge_def_t;
typedef struct input_def_s         input_def_t;
typedef struct request_def_s       request_def_t;

struct scripting_inpmgr_s {
    mrp_wayland_t *wl;
    const char *name;
    const char *display;
    const char *myappid;
    mrp_funcbridge_t *manager_update;
    mrp_funcbridge_t *input_update;
    mrp_funcbridge_t *code_update;
};

struct funcbridge_def_s {
    const char *name;
    const char *sign;
    mrp_funcbridge_cfunc_t func;
    void *data;
    mrp_funcbridge_t **ptr;
};

struct input_def_s {
    struct {
        const char *name;
        int32_t id;
    } device;
    mrp_wayland_input_type_t type;
    int32_t id;
    const char *appid;
    int32_t keycode;
};

struct request_def_s {
    const char *name;
    int offset;
    int mask;
    mrp_wayland_json_copy_func_t copy;
};



static int  input_manager_create(lua_State *);
static int  input_manager_connect(lua_State *);
static int  input_manager_canonical_name(lua_State *);
static int  input_manager_getfield(lua_State *);
static int  input_manager_setfield(lua_State *);
static void input_manager_destroy(void *);

static scripting_inpmgr_t *input_manager_check(lua_State *, int);

static input_def_t *input_def_check(lua_State *, int);
static void input_def_free(input_def_t *);

static input_def_t *switch_check(lua_State *, int, input_def_t *, size_t *);

static void manager_update_callback(mrp_wayland_t *,
                                    mrp_wayland_input_manager_operation_t,
                                    mrp_wayland_input_manager_t *);

static bool send_input_bridge(lua_State *, void *,
                              const char *, mrp_funcbridge_value_t *,
                              char *, mrp_funcbridge_value_t *);
static bool input_request_bridge(lua_State *, void *,
                                 const char *, mrp_funcbridge_value_t *,
                                 char *, mrp_funcbridge_value_t *);
static void input_update_callback(mrp_wayland_t *,
                                  mrp_wayland_input_operation_t,
                                  mrp_wayland_input_update_mask_t,
                                  mrp_wayland_input_t *);

static void code_update_callback(mrp_wayland_t *,
                                 mrp_wayland_code_operation_t,
                                 mrp_wayland_code_update_mask_t,
                                 mrp_wayland_code_t *);

static uint32_t copy_json_fields(mrp_wayland_t *, mrp_json_t *,
                                 request_def_t *, void *);

static bool register_methods(lua_State *);

static ssize_t get_own_appid(char *, ssize_t);


MRP_LUA_CLASS_DEF_SIMPLE (
    input_manager,                 /* class name */
    scripting_inpmgr_t,            /* userdata type */
    input_manager_destroy,         /* userdata destructor */
    MRP_LUA_METHOD_LIST (          /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR    (input_manager_create)
       MRP_LUA_METHOD(connect,        input_manager_connect)
       MRP_LUA_METHOD(canonical_name, input_manager_canonical_name)
    ),
    MRP_LUA_METHOD_LIST (          /* overrides */
       MRP_LUA_OVERRIDE_CALL         (input_manager_create)
       MRP_LUA_OVERRIDE_GETFIELD     (input_manager_getfield)
       MRP_LUA_OVERRIDE_SETFIELD     (input_manager_setfield)
    )
);

#if 0
static mrp_funcbridge_t *manager_request;
#endif
static mrp_funcbridge_t *send_input;
static mrp_funcbridge_t *input_request;

void mrp_wayland_scripting_input_manager_init(lua_State *L)
{
    MRP_ASSERT(L, "invalid argument");

    mrp_lua_create_object_class(L, INPUT_MANAGER_CLASS);
    register_methods(L);
}

mrp_wayland_t *mrp_wayland_scripting_input_manager_check(lua_State *L,int idx)
{
    scripting_inpmgr_t *imgr;
    mrp_wayland_t *wl;

    if ((imgr = input_manager_check(L, idx)) && (wl = imgr->wl)) {
        MRP_ASSERT(wl->scripting_input_data == (void *)imgr,
                   "confused with data structures");
        return wl;
    }

    return NULL;
}

mrp_wayland_t *mrp_wayland_scripting_input_manager_unwrap(void *void_imgr)
{
    scripting_inpmgr_t *imgr = (scripting_inpmgr_t *)void_imgr;

    if (imgr && mrp_lua_get_object_classdef(imgr) == INPUT_MANAGER_CLASS)
        return imgr->wl;

    return NULL;
}

static int input_manager_create(lua_State *L)
{
    mrp_context_t *ctx;
    mrp_wayland_t *wl;
    size_t fldnamlen;
    const char *fldnam;
    scripting_inpmgr_t *inpmgr;
    mrp_wayland_input_update_t iu;
    char *name;
    char myappid[512];
    const char *display = NULL;
    input_def_t *inputs = NULL;
    mrp_funcbridge_t *manager_update = NULL;
    mrp_funcbridge_t *input_update = NULL;
    mrp_funcbridge_t *code_update = NULL;
    char buf[256];
    input_def_t *i;
    const char *appid;
    bool permanent;
    int table;

    MRP_LUA_ENTER;

    ctx = mrp_lua_get_murphy_context();

    MRP_ASSERT(ctx && ctx->ml, "invalid app.context or missing mainloop");

    luaL_checktype(L, 2, LUA_TTABLE);

    inpmgr = (scripting_inpmgr_t*)mrp_lua_create_object(L, INPUT_MANAGER_CLASS,
                                                        NULL, 0);
    if (!inpmgr)
        luaL_error(L, "can't create input manager on display '%s'", display);

    table = lua_gettop(L);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        switch (mrp_wayland_scripting_field_name_to_type(fldnam, fldnamlen)) {

        case DISPLAY:
            display = luaL_checkstring(L, -1);
            break;

        case INPUTS:
            inputs = input_def_check(L, -1);
            break;

        case MANAGER_UPDATE:
            manager_update = mrp_funcbridge_create_luafunc(L, -1);
            break;

        case INPUT_UPDATE:
            input_update = mrp_funcbridge_create_luafunc(L, -1);
            break;

        case CODE_UPDATE:
            code_update = mrp_funcbridge_create_luafunc(L, -1);
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

    if (get_own_appid(myappid, sizeof(myappid)) <= 0) {
        mrp_debug("can't get own appid. falling back to hardwired 'murphyd'");
        snprintf(myappid, sizeof(myappid), "murphyd");
    }
    mrp_log_info("system-controller: own appid for weston is '%s'", myappid);

    if (!(wl = mrp_wayland_create(display, ctx->ml)))
        luaL_error(L, "can't create wayland object");

    mrp_lua_set_object_name(L, INPUT_MANAGER_CLASS, name);

    inpmgr->wl = wl;
    inpmgr->name = mrp_strdup(name);
    inpmgr->myappid = strdup(myappid);
    inpmgr->display = mrp_strdup(display);
    inpmgr->manager_update = manager_update;
    inpmgr->input_update = input_update;
    inpmgr->code_update = code_update;

#ifdef WESTON_ICO_PLUGINS
    mrp_ico_input_manager_register(wl);
#else
#endif

    mrp_wayland_register_input_manager_update_callback(wl,
                                                   manager_update_callback);
    mrp_wayland_register_input_update_callback(wl, input_update_callback);
    mrp_wayland_register_code_update_callback(wl,  code_update_callback);

    mrp_wayland_set_scripting_input_data(wl, inpmgr);

    mrp_wayland_create_scripting_inputs(wl, true);

    if (inputs) {
        memset(&iu, 0, sizeof(iu));
        iu.mask = MRP_WAYLAND_INPUT_DEVICE_NAME_MASK |
                  MRP_WAYLAND_INPUT_DEVICE_ID_MASK   |
                  MRP_WAYLAND_INPUT_TYPE_MASK        |
                  MRP_WAYLAND_INPUT_ID_MASK          |
                  MRP_WAYLAND_INPUT_PERMANENT_MASK   |
                  MRP_WAYLAND_INPUT_APPID_MASK       |
                  MRP_WAYLAND_INPUT_KEYCODE_MASK     ;

        for (i = inputs;  i->device.name;  i++) {
            appid = i->appid;
            permanent = false;

            if (i->appid) {
                permanent = true;
                if (!strcmp(appid, "murphy") || !strcmp(appid, "murphyd"))
                    appid = inpmgr->myappid;
            }

            iu.device.name = i->device.name;
            iu.device.id   = i->device.id;
            iu.type        = i->type;
            iu.id          = i->id;
            iu.permanent   = permanent;
            iu.appid       = appid;
            iu.keycode     = i->keycode;
            mrp_wayland_input_create(wl, &iu);
        }

        input_def_free(inputs);
    }

    lua_settop(L, table);

    MRP_LUA_LEAVE(1);
}

static int input_manager_connect(lua_State *L)
{
    scripting_inpmgr_t *imgr;
    bool success;

    MRP_LUA_ENTER;

    imgr = input_manager_check(L, 1);

    if (imgr->wl != NULL)
        success = mrp_wayland_connect(imgr->wl);
    else
        success = false;

    lua_pushboolean(L, success);

    MRP_LUA_LEAVE(1);
}

static int  input_manager_canonical_name(lua_State *L)
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

static int input_manager_getfield(lua_State *L)
{
    scripting_inpmgr_t *imgr;
    mrp_wayland_t *wl;
    const char *fldnam;
    mrp_wayland_scripting_field_t fld;

    MRP_LUA_ENTER;

    fld = mrp_wayland_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    imgr = input_manager_check(L, 1);

    if (!imgr || !(wl = imgr->wl))
        lua_pushnil(L);
    else {
        switch (fld) {

        case DISPLAY:
            lua_pushstring(L, imgr->display);
            break;

        case MANAGER_UPDATE:
            mrp_funcbridge_push(L, imgr->manager_update);
            break;

        case SEND_INPUT:
            mrp_funcbridge_push(L, send_input);
            break;

        case INPUT_REQUEST:
            mrp_funcbridge_push(L, input_request);
            break;

        case INPUT_UPDATE:
            mrp_funcbridge_push(L, imgr->input_update);
            break;

        case CODE_UPDATE:
            mrp_funcbridge_push(L, imgr->code_update);
            break;

        default:
            lua_pushstring(L, fldnam);
            lua_rawget(L, 1);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  input_manager_setfield(lua_State *L)
{
    scripting_inpmgr_t *imgr;

    MRP_LUA_ENTER;

    imgr = input_manager_check(L, 1);
    luaL_error(L, "input manager '%s' is read-only", imgr->display);

    MRP_LUA_LEAVE(0);
}

static void input_manager_destroy(void *data)
{
    scripting_inpmgr_t *inpmgr = (scripting_inpmgr_t *)data;

    MRP_LUA_ENTER;

    if (inpmgr) {
        mrp_free((void *)inpmgr->name);
        mrp_free((void *)inpmgr->display);
        mrp_free((void *)inpmgr->myappid);
    }

    MRP_LUA_LEAVE_NOARG;
}

static scripting_inpmgr_t *input_manager_check(lua_State *L, int idx)
{

    return (scripting_inpmgr_t *)mrp_lua_check_object(L, INPUT_MANAGER_CLASS,
                                                      idx);
}


static input_def_t *input_def_check(lua_State *L, int t)
{
    int d;
    size_t fldnamlen;
    const char *fldnam;
    mrp_wayland_scripting_field_t fld;
    size_t tlen;
    input_def_t *inputs, *i, *e;
    size_t ninput;
    size_t k, l;
    int32_t device_id;
    char *device_name;
    char errbuf[256];
    char buf[1024];

    t = (t < 0) ? lua_gettop(L) + t + 1 : t;
    inputs = NULL;
    ninput = 0;
    errbuf[0] = 0;

    luaL_checktype(L, t, LUA_TTABLE);
    tlen  = lua_objlen(L, t);

    for (k = 0;  k < tlen; k++) {
        lua_pushinteger(L, (int)(k+1));
        lua_gettable(L, t);

        d = lua_gettop(L);
        
        if (!lua_istable(L, d))
            goto error;

        l = ninput;

        device_id = -1;
        device_name = NULL;

        MRP_LUA_FOREACH_FIELD(L, d, fldnam, fldnamlen) {
            fld = mrp_wayland_scripting_field_name_to_type(fldnam, fldnamlen);
            
            switch (fld) {

            case ID:
                device_id = lua_tointeger(L,-1);
                break;

            case NAME:
                device_name = mrp_strdup(lua_tostring(L,-1));
                break;

            case SWITCH:
                if (!(inputs = switch_check(L, -1, inputs, &l)))
                    goto error;
                break;

            case POINTER:
            case KEYBOARD:
            case TOUCH:
            case HAPTIC:
                break;

            default:
                snprintf(errbuf,sizeof(errbuf),": invalid field '%s'",fldnam);
                goto error;
            }
        }

        lua_pop(L, 1);

        if (device_id < 0) {
            snprintf(errbuf,sizeof(errbuf),": missing id field");
            goto error;
        }
        if (!device_name) {
            snprintf(errbuf,sizeof(errbuf),": missing name field");
            goto error;
        }
        if (l <= ninput) {
            snprintf(errbuf,sizeof(errbuf),": no switch or other input");
            goto error;
        }

        for (i = inputs + ninput, e = inputs + l;   i < e;   i++) {
            i->device.name = mrp_strdup(device_name);
            i->device.id = device_id;

            if (!i->device.name)
                luaL_error(L, "can't allocate memory");
        }

        mrp_free(device_name);

        ninput = l;
    }

    return inputs;

 error:
    input_def_free(inputs);
    snprintf(buf, sizeof(buf), "malformed input definition%s", errbuf);
    luaL_argerror(L, k+1, buf);
    return NULL;
}



static void input_def_free(input_def_t *inputs)
{
    input_def_t *i;

    if (inputs) {
        for (i = inputs;  i->device.name;  i++) {
            mrp_free((void *)i->device.name);
            mrp_free((void *)i->appid);
        }
        mrp_free((void *)inputs);
    }
}

static input_def_t *switch_check(lua_State *L,
                                 int t,
                                 input_def_t *inputs,
                                 size_t *plen)
{
    int d;
    input_def_t *i;
    int32_t id;
    const char *appid;
    int32_t keycode;
    size_t l;
    size_t fldnamlen;
    const char *fldnam;

    t = (t < 0) ? lua_gettop(L) + t + 1 : t;
    l = *plen;

    luaL_checktype(L, t, LUA_TTABLE);

    for (lua_pushnil(L);  lua_next(L,t);  lua_pop(L,1), l++) {
        d = lua_gettop(L);

        if (lua_type(L,-2) != LUA_TNUMBER || lua_type(L,d) != LUA_TTABLE)
            goto error;

        id = lua_tointeger(L,-2);

        if (id < 0 || id >= 1000)
            goto error;

        appid = NULL;
        keycode = 0;

        MRP_LUA_FOREACH_FIELD(L, d, fldnam, fldnamlen) {
            switch(mrp_wayland_scripting_field_name_to_type(fldnam,fldnamlen)){
            case APPID:    appid   = lua_tostring(L, -1);   break;
            case KEYCODE:  keycode = lua_tointeger(L, -1);  break;
            default:                                        goto error;
            }
        }

        if (!(inputs = mrp_reallocz(inputs, l, l+2)))
            luaL_error(L, "can't allocate memory for switch");

        i = inputs + l;

        i->type = MRP_WAYLAND_INPUT_TYPE_SWITCH;
        i->id = id;
        i->appid = mrp_strdup(appid);
        i->keycode = keycode;

        if (!i->appid)
            luaL_error(L, "can't allocate memory for switch");
    }

    *plen = l;

    return inputs;

 error:
    input_def_free(inputs);
    luaL_argerror(L, l+1, "malformed switch definition");
    *plen = 0;
    return NULL;
}

static void manager_update_callback(mrp_wayland_t *wl,
                                   mrp_wayland_input_manager_operation_t oper,
                                   mrp_wayland_input_manager_t *im)
{
    lua_State *L;
    scripting_inpmgr_t *inpmgr;
    mrp_funcbridge_value_t args[4], ret;
    char t;
    bool success;

    MRP_ASSERT(wl && im, "invalid argument");

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("sysem-controller: can't update manager: "
                      "LUA is not initialesed");
        return;
    }

    if (!(inpmgr = (scripting_inpmgr_t *)wl->scripting_input_data)) {
        mrp_log_error("system-controller: input manager "
                      "scripting is not initialized");
        return;
    }

    MRP_ASSERT(wl == inpmgr->wl, "confused with data structures");

    args[0].pointer = inpmgr;
    args[1].integer = oper;

    memset(&ret, 0, sizeof(ret));

    success = mrp_funcbridge_call_from_c(L, inpmgr->manager_update, "od",
                                         args, &t, &ret);
    if (!success) {
        mrp_log_error("failed to call input_manager.%s.manager_update method "
                      "(%s)", inpmgr->name, ret.string ? ret.string : "NULL");
        mrp_free((void *)ret.string);
    }
}

static bool send_input_bridge(lua_State *L,
                              void *data,
                              const char *signature,
                              mrp_funcbridge_value_t *args,
                              char *ret_type,
                              mrp_funcbridge_value_t *ret_val)
{
#define FIELD(f) MRP_OFFSET(mrp_wayland_input_event_t, f)
#define MASK(m)  MRP_WAYLAND_INPUT_ ## m ## _MASK
#define TYPE(t)  mrp_wayland_json_ ## t ## _copy

    static request_def_t   fields[] = {
        { "deviceno"  , FIELD(device.id), MASK(DEVICE_ID), TYPE(integer) },
        { "surface"   , FIELD(surfaceid), 0x10000        , TYPE(integer) },
        { "ev_time"   , FIELD(time)     , 0x20000        , TYPE(integer) },
        { "ev_code"   , FIELD(codeid)   , 0x40000        , TYPE(integer) },
        { "ev_value"  , FIELD(value)    , 0x80000        , TYPE(integer) },
        {    NULL     ,        0        ,    0           ,      NULL     }
    };

#undef FIELD
#undef MASK
#undef TYPE

    mrp_wayland_t *wl;
    mrp_wayland_input_event_t ev;
    mrp_json_t *json;
    uint32_t mask;

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

    if (!(wl = mrp_wayland_scripting_input_manager_unwrap(args[0].pointer))) {
        mrp_log_error("system-controller: argument 1 is not a "
                      "'input_manager' class object");
        return false;
    }

    if (!(json = mrp_json_lua_unwrap(args[1].pointer))) {
        mrp_log_error("system-controller: argument 2 is not a "
                      "'JSON' class object");
        return false;
    }

    memset(&ev, 0, sizeof(ev));
    mask = copy_json_fields(wl, json, fields, &ev);
    ev.device.mask = mask & 0xffff;

    mrp_wayland_send_input(wl, &ev);

    return true;
}


static bool input_request_bridge(lua_State *L,
                                  void *data,
                                  const char *signature,
                                  mrp_funcbridge_value_t *args,
                                  char *ret_type,
                                  mrp_funcbridge_value_t *ret_val)
{
#define FIELD(f) MRP_OFFSET(mrp_wayland_input_update_t, f)
#define MASK(m)  MRP_WAYLAND_INPUT_ ## m ## _MASK
#define TYPE(t)  mrp_wayland_json_ ## t ## _copy

    static request_def_t   fields[] = {
        { "device"    , FIELD(device.name), MASK(DEVICE_NAME), TYPE(string)  },
        { "input"     , FIELD(id)         , MASK(ID)         , TYPE(integer) },
        { "appid"     , FIELD(appid)      , MASK(APPID)      , TYPE(string)  },
        { "alloc_type", FIELD(permanent)  , MASK(PERMANENT)  , TYPE(boolean) },
        {    NULL     ,        0          ,      0           ,      NULL     }
    };

#undef FIELD
#undef MASK
#undef TYPE

    mrp_wayland_t *wl;
    mrp_wayland_input_update_t u;
    mrp_json_t *json;

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

    if (!(wl = mrp_wayland_scripting_input_manager_unwrap(args[0].pointer))) {
        mrp_log_error("system-controller: argument 1 is not a "
                      "'input_manager' class object");
        return false;
    }

    if (!(json = mrp_json_lua_unwrap(args[1].pointer))) {
        mrp_log_error("system-controller: argument 2 is not a "
                      "'JSON' class object");
        return false;
    }
        
    memset(&u, 0, sizeof(u));
    u.mask = copy_json_fields(wl, json, fields, &u);

    mrp_wayland_input_request(wl, &u);

    return true;
}


static void input_update_callback(mrp_wayland_t *wl,
                                   mrp_wayland_input_operation_t oper,
                                   mrp_wayland_input_update_mask_t mask,
                                   mrp_wayland_input_t *inp)
{
    lua_State *L;
    scripting_inpmgr_t *inpmgr;
    mrp_funcbridge_value_t args[4], ret;
    char t;
    bool success;

    MRP_ASSERT(wl && inp, "invalid argument");

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("system-controller: can't update input %u: "
                      "LUA is not initialesed", inp->id);
        return;
    }

    if (!(inpmgr = (scripting_inpmgr_t *)wl->scripting_input_data)) {
        mrp_log_error("system-controller: input manager scripting is "
                      "not initialized");
        return;
    }

    MRP_ASSERT(wl == inpmgr->wl, "confused with data structures");

    if (!inp->scripting_data) {
        mrp_log_error("system-controller: no scripting data for "
                      "input %d", inp->id);
        return;
    }

    args[0].pointer = inpmgr;
    args[1].integer = oper;
    args[2].pointer = inp->scripting_data;
    args[3].pointer = mrp_wayland_scripting_input_mask_create_from_c(L, mask);

    memset(&ret, 0, sizeof(ret));

    success = mrp_funcbridge_call_from_c(L, inpmgr->input_update, "odoo",
                                         args, &t, &ret);
    if (!success) {
        mrp_log_error("failed to call input_manager.%s.input_update method "
                      "(%s)", inpmgr->name, ret.string ? ret.string : "NULL");
        mrp_free((void *)ret.string);
    }
}

static void code_update_callback(mrp_wayland_t *wl,
                                 mrp_wayland_code_operation_t oper,
                                 mrp_wayland_code_update_mask_t mask,
                                 mrp_wayland_code_t *code)
{
    lua_State *L;
    scripting_inpmgr_t *inpmgr;
    mrp_funcbridge_value_t args[4], ret;
    char t;
    bool success;

    MRP_ASSERT(wl && code, "invalid argument");

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("system-controller: can't update input %u: "
                      "LUA is not initialesed", code->id);
        return;
    }

    if (!(inpmgr = (scripting_inpmgr_t *)wl->scripting_input_data)) {
        mrp_log_error("system-controller: input manager scripting is "
                      "not initialized");
        return;
    }

    MRP_ASSERT(wl == inpmgr->wl, "confused with data structures");

    if (!code->scripting_data) {
        mrp_log_error("system-controller: no scripting data for "
                      "code %d", code->id);
        return;
    }

    args[0].pointer = inpmgr;
    args[1].integer = oper;
    args[2].pointer = code->scripting_data;
    args[3].pointer = mrp_wayland_scripting_code_mask_create_from_c(L, mask);

    memset(&ret, 0, sizeof(ret));

    success = mrp_funcbridge_call_from_c(L, inpmgr->code_update, "odoo",
                                         args, &t, &ret);
    if (!success) {
        mrp_log_error("failed to call input_manager.%s.code_update method "
                      "(%s)", inpmgr->name, ret.string ? ret.string : "NULL");
        mrp_free((void *)ret.string);
    }
}

#if 0
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

    if (!(wl = mrp_wayland_scripting_input_manager_unwrap(args[0].pointer))) {
        mrp_log_error("argument 1 is not a 'input_manager' class object");
        return false;
    }

    if (!(json = mrp_json_lua_unwrap(args[1].pointer))) {
        mrp_log_error("argument 2 is not a 'JSON' class object");
        return false;
    }

    memset(&u, 0, sizeof(u));
    u.mask = copy_json_fields(wl, json, fields, &u);

    mrp_wayland_area_create(wl, &u);

    return true;
}
#endif

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
        FUNCBRIDGE(send_input   , "oo", NULL),
        FUNCBRIDGE(input_request, "oo", NULL),
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

/*
 * This is needed to get the same appid what ico-weston-plugin uses.
 * particularily useful when launching murphy eg. 'murphyd -f -vvv -d @file.c'
 * and appid weston will be 'murphyd@file.c'
 */
static ssize_t get_own_appid(char *buf, ssize_t len)
{
    int fd;
    ssize_t size, l;
    char *p, *q, *e, c;
    char path[64];
    char cmdline[128]; /* ico-weston-plugin has this size */

    *buf = 0;

    snprintf(path, sizeof(path), "/proc/%d/cmdline", getpid());

    if ((fd = open(path, O_RDONLY)) < 0)
        return 0;

    for (;;) {
        size = read(fd, cmdline, sizeof(cmdline));

        if (size < 0) {
            if (errno != EINTR)
                break;
        }
        else {
            while (size > 0 && !cmdline[size-1])
                size--;

            if (size < 1)
                break;

            /* copy basename to buf */
            for (p = cmdline, e = (q = buf) + len-1;  q < e && (c = *p);  p++){
                if (c == '/')
                    q = buf;
                else
                    *q++ = c;
            }
            *q = 0;

            /* do as ico-weston-plugin for searching 'app start option' */
            /*for (e = cmdline + size-1, l = len - (q - buf);   p < e;   p++)*/
            for (e = cmdline + size, l = len - (q - buf);   p < e;   p++) {
                if (p[0] == 0 && p[1] == '@')
                    strncpy(q, p+1, e-p < l ? e-p : l);
            }
            buf[len-1] = 0;

            break;
        }
    }

    close(fd);

    /* strip possible garbage from the end */
    for (q = buf + strlen(buf);  q >= buf;  q--) {
        if (*q <= 0x20)
            *q = 0;
        else
            break;
    }

    return (q+1) - buf;
}
