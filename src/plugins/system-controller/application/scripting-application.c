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

#include "wayland/wayland.h"
#include "wayland/area.h"

/* TODO: this shoud actually go to some common scripting */
char *mrp_wayland_scripting_canonical_name(const char *, char *, size_t);


#define APPLICATION_CLASS      MRP_LUA_CLASS_SIMPLE(application)
#define REQUISITE_CLASS        MRP_LUA_CLASS_SIMPLE(requisite)

typedef enum mrp_sysctl_scripting_field_e  field_t;
typedef struct scripting_app_s  scripting_app_t;
typedef struct scripting_req_s  scripting_req_t;


struct scripting_app_s {
    mrp_application_t *app;
    char *id;
};

struct scripting_req_s {
    mrp_application_requisite_t rq;
};



static int  app_create_from_lua(lua_State *);
static int  app_getfield(lua_State *);
static int  app_setfield(lua_State *);
static int  app_stringify(lua_State *);
static void app_destroy_from_lua(void *);

static scripting_app_t *app_check(lua_State *, int);
static int  app_lookup(lua_State *L);

static scripting_req_t *req_create_from_c(lua_State *,
                                          mrp_application_requisite_t);
static int  req_create_from_lua(lua_State *);
static int  req_getfield(lua_State *);
static int  req_setfield(lua_State *);
static int  req_stringify(lua_State *);
static void req_destroy_from_lua(void *);

static scripting_req_t *req_check(lua_State *, int);

mrp_application_requisite_t get_requisite(field_t);

static mrp_application_privileges_t *priv_check(lua_State *, int);
static void priv_free(mrp_application_privileges_t *);
static int  priv_push(lua_State *L, mrp_application_privileges_t *);

static mrp_application_requisites_t *reqs_check(lua_State *, int);
static void reqs_free(mrp_application_requisites_t *);
static int reqs_push(lua_State *, mrp_application_requisites_t *);

static int reqs_entry_check(lua_State *, int);


static field_t field_check(lua_State *, int, const char **);
static field_t field_name_to_type(const char *, ssize_t);


MRP_LUA_CLASS_DEF_SIMPLE (
    application,                  /* class name */
    scripting_app_t,              /* userdata type */
    app_destroy_from_lua,         /* userdata destructor */
    MRP_LUA_METHOD_LIST (         /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR   (app_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (         /* overrides */
       MRP_LUA_OVERRIDE_CALL        (app_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD    (app_getfield)
       MRP_LUA_OVERRIDE_SETFIELD    (app_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY   (app_stringify)
    )
);

MRP_LUA_CLASS_DEF_SIMPLE (
    requisite,                    /* class name */
    scripting_req_t,              /* userdata type */
    req_destroy_from_lua,         /* userdata destructor */
    MRP_LUA_METHOD_LIST (         /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR   (req_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (         /* overrides */
       MRP_LUA_OVERRIDE_CALL        (req_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD    (req_getfield)
       MRP_LUA_OVERRIDE_SETFIELD    (req_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY   (req_stringify)
    )
);


void mrp_application_scripting_init(lua_State *L)
{
    mrp_lua_create_object_class(L, APPLICATION_CLASS);
    mrp_lua_create_object_class(L, REQUISITE_CLASS);

    lua_pushcfunction(L, app_lookup);
    lua_setglobal(L, "application_lookup");
}

mrp_application_t *mrp_application_scripting_app_check(lua_State *L, int idx)
{
    scripting_app_t *a;
    mrp_application_t *app;

    if ((a = app_check(L, idx)) && (app = a->app)) {
        MRP_ASSERT(app->scripting_data == (void *)a,
                   "confused with data structures");
        return app;
    }

    return NULL;
}

mrp_application_t *mrp_application_scripting_app_unwrap(void *void_a)
{
    scripting_app_t *a = (scripting_app_t *)void_a;

    if (a && mrp_lua_get_object_classdef(a) == APPLICATION_CLASS)
        return a->app;

    return NULL;
}


void *mrp_application_scripting_app_create_from_c(mrp_application_t *app)
{
    lua_State *L;
    scripting_app_t *a;

    MRP_ASSERT(app, "invald argument");
    
    if (app->scripting_data)
        a = app->scripting_data;
    else {
        if (!(L = mrp_lua_get_lua_state())) {
            mrp_log_error("can't create scripting application '%s': LUA is "
                          "not initialized", app->appid);
            return NULL;
        }

        a = (scripting_app_t *)mrp_lua_create_object(L, APPLICATION_CLASS,
                                                     app->appid, 0);
        if (!a) {
            mrp_log_error("can't create scripting application '%s': "
                          "LUA object creation failed", app->appid);
        }
        else {
            a->app = app;
            mrp_application_set_scripting_data(app, a);
            lua_pop(L, 1);
        }
    }

    return a;
}


void mrp_application_scripting_app_destroy_from_c(mrp_application_t *app)
{
    lua_State *L;
    scripting_app_t *a;

    MRP_ASSERT(app, "invalid argument");

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't destroy scripting application '%s': "
                      "LUA is not initialized", app->appid);
        return;
    }

    if ((a = app->scripting_data)) {
        mrp_debug("destroy scripting application '%s'", app->appid);

        a->app = NULL;
        mrp_application_set_scripting_data(app, NULL);

        mrp_lua_destroy_object(L, a->id,0, a);
    }
}


static int app_create_from_lua(lua_State *L)
{
    mrp_application_update_t u;
    mrp_application_t *app;
    scripting_app_t *a;
    size_t fldnamlen;
    const char *fldnam;
    const char *appid = NULL;
    const char *arnam = "default";
    const char *class = NULL;
    int32_t spri = 0;
    mrp_application_privileges_t *privs = NULL;
    mrp_application_requisites_t *reqs = NULL;
    char *id;
    char buf[4096];


    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        switch (field_name_to_type(fldnam, fldnamlen)) {

        case APPID:           appid = luaL_checkstring(L, -1);           break;
        case AREA:            arnam = luaL_checkstring(L, -1);           break;
        case PRIVILEGES:      privs = priv_check(L, -1);                 break;
        case REQUISITES:      reqs  = reqs_check(L, -1);                 break;
        case RESOURCE_CLASS:  class = luaL_checkstring(L, -1);           break;
        case SCREEN_PRIORITY: spri  = luaL_checkinteger(L, -1);          break;
        default:              luaL_error(L, "bad field '%s'", fldnam);   break;
        }
    }

    if (!appid)
        luaL_error(L, "'appid' field is missing"); 
    if (!class)
        luaL_error(L, "'resource_class' field is missing");
    if (spri < 0 || spri > 255)
        luaL_error(L, "'screen_priority' is out of range (0 - 255)");

    id = mrp_wayland_scripting_canonical_name(appid, buf, sizeof(buf));
    a = (scripting_app_t*)mrp_lua_create_object(L, APPLICATION_CLASS, id, 0);

    if (!(a))
        luaL_error(L, "can't create scripting application '%s'", appid);

    u.mask = MRP_APPLICATION_APPID_MASK           |
             MRP_APPLICATION_AREA_NAME_MASK       |
             MRP_APPLICATION_RESOURCE_CLASS_MASK  |
             MRP_APPLICATION_SCREEN_PRIORITY_MASK ;
    u.appid = appid;
    u.area_name = arnam;
    u.resource_class = class;
    u.screen_priority = spri;

    if (privs) {
        u.mask |= MRP_APPLICATION_PRIVILEGES_MASK;
        u.privileges.screen = privs->screen;
        u.privileges.audio = privs->audio;
        priv_free(privs);
    }

    if (reqs) {
        u.mask |= MRP_APPLICATION_REQUISITES_MASK;
        u.requisites.screen = reqs->screen;
        u.requisites.audio = reqs->audio;
        reqs_free(reqs);
    }


    if ((app = mrp_application_create(&u, a))) {
        a->app = app;
        a->id = mrp_strdup(id);
    }
    else {
        lua_pop(L, 1);
        mrp_lua_destroy_object(L, id,0, a);
        lua_pushnil(L);
    }

    MRP_LUA_LEAVE(1);
}

static int app_getfield(lua_State *L)
{
    scripting_app_t *a;
    mrp_application_t *app;
    mrp_wayland_area_t *area;
    const char *fldnam;
    field_t fld;

    MRP_LUA_ENTER;

    a = app_check(L, 1);
    fld = field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    if (!a || !(app = a->app))
        lua_pushnil(L);
    else {
        area = app->area;

        switch (fld) {
        case APPID:
            lua_pushstring(L, app->appid ? app->appid : "");
            break;
        case AREA:
            lua_pushstring(L, area ? area->fullname : "");
            break;
        case PRIVILEGES:
            priv_push(L, &app->privileges);
            break;
        case REQUISITES:
            reqs_push(L, &app->requisites);
            break;
        case RESOURCE_CLASS:
            lua_pushstring(L, app->resource_class ? app->resource_class : "");
            break;
        case SCREEN_PRIORITY:
            lua_pushinteger(L, app->screen_priority);
            break;
        default:
            lua_pushnil(L);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  app_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    app_check(L, 1);
    luaL_error(L, "application objects are read-only");

    lua_pop(L, 2);

    MRP_LUA_LEAVE(0);
}

static int  app_stringify(lua_State *L)
{
#define ALL_FIELDS (MRP_APPLICATION_END_MASK - 1)

    scripting_app_t *a;
    mrp_application_t *app;
    char *p, *e;
    char buf[4096];

    MRP_LUA_ENTER;

    a = app_check(L, 1);

    if (!(app = a->app))
        lua_pushnil(L);
    else {
        e = (p = buf) + sizeof(buf);
        p += snprintf(p, e-p, "application '%s'", app->appid);
        p += mrp_application_print(app, ALL_FIELDS, p, e-p);

        lua_pushlstring(L, buf, p-buf);
    }

    MRP_LUA_LEAVE(1);

#undef ALL_FIELDS
}

static void app_destroy_from_lua(void *data)
{
    scripting_app_t *a = (scripting_app_t *)data;
    mrp_application_t *app;

    MRP_LUA_ENTER;

    if (a && (app = a->app)) {
        a->app = NULL;
        mrp_application_set_scripting_data(app, NULL);
        mrp_free(a->id);
    }

    MRP_LUA_LEAVE_NOARG;
}


static scripting_app_t *app_check(lua_State *L, int idx)
{
    return (scripting_app_t *)mrp_lua_check_object(L, APPLICATION_CLASS, idx);
}

static int app_lookup(lua_State *L)
{
    mrp_application_t *app;
    const char *appid;
    void *sa;

    MRP_LUA_ENTER;

    if (!lua_isstring(L, 1))
        lua_pushnil(L);
    else {
        appid = lua_tostring(L, 1);

        if ((app = mrp_application_find(appid)) && (sa = app->scripting_data))
            mrp_lua_push_object(L, sa);
        else
            lua_pushnil(L);
    }

    MRP_LUA_LEAVE(1);
}


void *
mrp_application_scripting_req_create_from_c(mrp_application_requisite_t rq)
{
    lua_State *L;
    scripting_req_t *r;

    if (!(L = mrp_lua_get_lua_state())) {
        mrp_log_error("can't create scripting requisites: "
                      "LUA is not initialized");
        return NULL;
    }

    if ((r = req_create_from_c(L, rq)))
        lua_pop(L, 1);

    return r;
}

mrp_application_requisite_t *mrp_application_scripting_req_unwrap(void *void_r)
{
    scripting_req_t *r = (scripting_req_t *)void_r;

    if (r && mrp_lua_get_object_classdef(r) == REQUISITE_CLASS)
        return &r->rq;

    return NULL;
}

static scripting_req_t *req_create_from_c(lua_State *L,
                                          mrp_application_requisite_t rq)
{
    scripting_req_t *r;

    r = (scripting_req_t *)mrp_lua_create_object(L, REQUISITE_CLASS, NULL, 0);
    
    if (r)
        r->rq = rq;
    else {
        mrp_log_error("can't create scripting requisites: "
                      "LUA object creation failed");
    }

    return r;
}

static int req_create_from_lua(lua_State *L)
{
    size_t fldnamlen;
    const char *fldnam;
    scripting_req_t *r;
    field_t fld, val;
    mrp_application_requisite_t rq = 0;

    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        fld = field_name_to_type(fldnam, fldnamlen);
        
        switch (fld) {

        case MASK:
            rq = lua_tointeger(L, -1);
            break;

        case NONE:
            if (!(val = lua_toboolean(L, -1)))
                rq |= (MRP_APPLICATION_REQUISITE_MAX - 1);
            break;

        case DRIVING:
            if ((val = lua_toboolean(L, -1)))
                rq |= MRP_APPLICATION_REQUISITE_DRIVING;
            else
                rq &= ~MRP_APPLICATION_REQUISITE_DRIVING;
            break;

        case PARKED:
            if ((val = lua_toboolean(L, -1)))
                rq |= MRP_APPLICATION_REQUISITE_PARKED;
            else
                rq &= ~MRP_APPLICATION_REQUISITE_PARKED;
            break;

        case REVERSES:
            if ((val = lua_toboolean(L, -1)))
                rq |= MRP_APPLICATION_REQUISITE_REVERSES;
            else
                rq &= ~MRP_APPLICATION_REQUISITE_REVERSES;
            break;

        case BLINKER_LEFT:
            if ((val = lua_toboolean(L, -1)))
                rq |= MRP_APPLICATION_REQUISITE_BLINKER_LEFT;
            else
                rq &= ~MRP_APPLICATION_REQUISITE_BLINKER_LEFT;
            break;

        case BLINKER_RIGHT:
            if ((val = lua_toboolean(L, -1)))
                rq |= MRP_APPLICATION_REQUISITE_BLINKER_RIGHT;
            else
                rq &= ~MRP_APPLICATION_REQUISITE_BLINKER_RIGHT;
            break;

        default:
            luaL_error(L, "invalid prerequiste '%s'", fldnam);
            break;
        }
    }

    r = (scripting_req_t *)mrp_lua_create_object(L, REQUISITE_CLASS, NULL, 0);

    if (!r)
        luaL_error(L, "can't create requisite");

    r->rq = rq;

    MRP_LUA_LEAVE(1);
}

static int req_getfield(lua_State *L)
{
    scripting_req_t *r;
    field_t fld;
    mrp_application_requisite_t mask;

    MRP_LUA_ENTER;

    r = req_check(L, 1);
    fld = field_check(L, 2, NULL);

    lua_pop(L, 1);

    if (!r)
        lua_pushnil(L);
    else {
        switch (fld) {
        case MASK:
            lua_pushinteger(L, r->rq);
            break;
        case NONE:
            lua_pushboolean(L, r->rq ? 0 : 1);
            break;
        case DRIVING:
            mask = MRP_APPLICATION_REQUISITE_DRIVING;
            goto push_value;
        case PARKED:
            mask = MRP_APPLICATION_REQUISITE_PARKED;
            goto push_value;
        case REVERSES:
            mask = MRP_APPLICATION_REQUISITE_REVERSES;
            goto push_value;
        case BLINKER_LEFT:
            mask = MRP_APPLICATION_REQUISITE_BLINKER_LEFT;
            goto push_value;
        case BLINKER_RIGHT:
            mask = MRP_APPLICATION_REQUISITE_BLINKER_RIGHT;
            goto push_value;
        default:
            lua_pushnil(L);
            break;
        push_value:
            lua_pushboolean(L, (r->rq & mask) ? 1 : 0);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int req_setfield(lua_State *L)
{
    scripting_req_t *r;
    const char *fldstr;
    field_t fld;
    mrp_application_requisite_t mask;

    MRP_LUA_ENTER;

    r = req_check(L, 1);
    fld = field_check(L, 2, &fldstr);

    lua_pop(L, 2);

    if (r) {
        switch (fld) {
        case MASK:
            r->rq = lua_tointeger(L, 3);
            break;
        case DRIVING:
            mask = MRP_APPLICATION_REQUISITE_DRIVING;
            goto set_value;
        case PARKED:
            mask = MRP_APPLICATION_REQUISITE_PARKED;
            goto set_value;
        case REVERSES:
            mask = MRP_APPLICATION_REQUISITE_REVERSES;
            goto set_value;
        case BLINKER_LEFT:
            mask = MRP_APPLICATION_REQUISITE_BLINKER_LEFT;
            goto set_value;
        case BLINKER_RIGHT:
            mask = MRP_APPLICATION_REQUISITE_BLINKER_RIGHT;
            goto set_value;
        default:
            break;
        set_value:
            if (lua_toboolean(L, 3))
                r->rq |= mask;
            else
                r->rq &= ~mask;
            break;
        }
    }

    MRP_LUA_LEAVE(0);
}

static int req_stringify(lua_State *L)
{
    scripting_req_t *r;
    char buf[4096];

    MRP_LUA_ENTER;

    r = req_check(L, 1);

    mrp_application_requisite_print(r->rq, buf, sizeof(buf));

    lua_pushstring(L, buf);

    MRP_LUA_LEAVE(1);
}

static void req_destroy_from_lua(void *data)
{
    scripting_req_t *r = (scripting_req_t *)data;

    MRP_UNUSED(r);
}

static scripting_req_t *req_check(lua_State *L, int idx)
{
    return (scripting_req_t *)mrp_lua_check_object(L, REQUISITE_CLASS, idx);
}


mrp_application_requisite_t get_requisite(field_t fld)
{
    switch(fld) {
    case NONE:            return MRP_APPLICATION_REQUISITE_NONE;
    case DRIVING:         return MRP_APPLICATION_REQUISITE_DRIVING;
    case PARKED:          return MRP_APPLICATION_REQUISITE_PARKED;
    case REVERSES:        return MRP_APPLICATION_REQUISITE_REVERSES;
    case BLINKER_LEFT:    return MRP_APPLICATION_REQUISITE_BLINKER_LEFT; 
    case BLINKER_RIGHT:   return MRP_APPLICATION_REQUISITE_BLINKER_RIGHT;
    default:              return 0;
    }
}

static mrp_application_privileges_t *priv_check(lua_State *L, int idx)
{
    mrp_application_privileges_t *priv;
    int *p, v;
    int screen = -1;
    int audio = -1;
    int n;
    size_t fldnamlen, privnamlen;
    const char *fldnam, *privnam;

    idx = mrp_lua_absidx(L, idx);

    luaL_checktype(L, idx, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, idx, fldnam, fldnamlen) {
        switch (field_name_to_type(fldnam, fldnamlen)) {

        case SCREEN:
            p = &screen;
            goto get_privilege;

        case AUDIO:
            p = &audio;
            goto get_privilege;

        default:
            luaL_error(L, "bad field '%s'", fldnam);
            break;

        get_privilege:
            v = -1;
            if (lua_isnumber(L, -1)) {
                n = lua_tointeger(L, -1);
                if (n >= 0 && n < MRP_APPLICATION_PRIVILEGE_MAX)
                    v = n;
            }
            else if ((privnam = lua_tolstring(L, -1, &privnamlen))) {
                switch (privnamlen) {
                case 4:
                    if (!strcmp(privnam, "none"))
                        v = MRP_APPLICATION_PRIVILEGE_NONE;
                    break;
                case 6:
                    if (!strcmp(privnam, "system"))
                        v = MRP_APPLICATION_PRIVILEGE_SYSTEM;
                    break;
                case 9:
                    if (!strcmp(privnam, "unlimited"))
                        v = MRP_APPLICATION_PRIVILEGE_UNLIMITED;
                    else if (!strcmp(privnam, "certified"))
                        v = MRP_APPLICATION_PRIVILEGE_CERTIFIED;
                    break;
                case 12:
                    if (!strcmp(privnam, "manufacturer"))
                        v = MRP_APPLICATION_PRIVILEGE_MANUFACTURER;
                    break;
                default:
                    break;
                } /* switch privnamlen */
            }
            *p = v;
            break;
        } /* switch fldnam */
    } /* FOREACH_FIELD */

    if (screen < 0)
        luaL_error(L, "missing or invalid 'screen' field");
    if (audio < 0)
        luaL_error(L, "missing or invalid 'audio' field");

    if ((priv = mrp_allocz(sizeof(*priv)))) {
        priv->screen = screen;
        priv->audio = audio;
    }

    return priv;
}

static void priv_free(mrp_application_privileges_t *priv)
{
    mrp_free(priv);
}

static int priv_push(lua_State *L, mrp_application_privileges_t *privs)
{
    if (!privs)
        lua_pushnil(L);
    else {
        lua_createtable(L, 0, 2);

        lua_pushstring(L, "screen");
        lua_pushstring(L, mrp_application_privilege_str(privs->screen));
        lua_settable(L, -3);

        lua_pushstring(L, "audio");
        lua_pushstring(L, mrp_application_privilege_str(privs->audio));
        lua_settable(L, -3);
    }

    return 1;
}

static mrp_application_requisites_t *reqs_check(lua_State *L, int idx)
{
    mrp_application_requisites_t *reqs;
    int screen = -1;
    int audio = -1;
    size_t fldnamlen;
    const char *fldnam;

    idx = mrp_lua_absidx(L, idx);

    luaL_checktype(L, idx, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, idx, fldnam, fldnamlen) {
        switch (field_name_to_type(fldnam, fldnamlen)) {

        case SCREEN:  screen = reqs_entry_check(L, -1);          break;
        case AUDIO:   audio  = reqs_entry_check(L, -1);          break;
        default:      luaL_error(L, "bad field '%s'", fldnam);   break;

        } /* switch fldnam */
    } /* FOREACH_FIELD */

    if (screen < 0)
        luaL_error(L, "missing or invalid 'screen' field");
    if (audio < 0)
        luaL_error(L, "missing or invalid 'audio' field");

    if ((reqs = mrp_allocz(sizeof(*reqs)))) {
        reqs->screen = screen;
        reqs->audio = audio;
    }

    return reqs;
}

static void reqs_free(mrp_application_requisites_t *reqs)
{
    mrp_free(reqs);
}

static int reqs_push(lua_State *L, mrp_application_requisites_t *reqs)
{
    if (!reqs)
        lua_pushnil(L);
    else {
        lua_createtable(L, 0, 2);

        lua_pushstring(L, "screen");
        req_create_from_c(L, reqs->screen);
        lua_settable(L, -3);

        lua_pushstring(L, "audio");
        req_create_from_c(L, reqs->audio);
        lua_settable(L, -3);
    }

    return 1;
}

static int reqs_entry_check(lua_State *L, int idx)
{
    int r;
    int type;
    int i, j, n;
    const char *name;

    r = 0;
    idx = mrp_lua_absidx(L, idx);
    type = lua_type(L, idx);

    if (type == LUA_TTABLE) {
        j = -1;
        n = lua_objlen(L, idx);
    }
    else if (type == LUA_TSTRING) {
        j = idx;
        n = 1;
    }
    else if (type == LUA_TNUMBER) {
        r = lua_tointeger(L, idx);
        if (r < 0 || r >= MRP_APPLICATION_REQUISITE_MAX) {
            luaL_error(L, "requisite mask is out of range (0 - %d)",
                       MRP_APPLICATION_REQUISITE_MAX - 1);
            r = -1;
        }
        n = 0;
    }
    else {
        luaL_error(L, "requisite type must be either table or string");
        n = 0;
        r = -1;
    }

    for (i = 0;  i < n;  i++) {
        if (type == LUA_TTABLE) {
            lua_pushinteger(L, i+1);
            lua_gettable(L, idx);
        }

        switch (field_check(L, j, &name)) {
        case NONE:          r = MRP_APPLICATION_REQUISITE_NONE;          break;
        case DRIVING:       r = MRP_APPLICATION_REQUISITE_DRIVING;       break;
        case PARKED:        r = MRP_APPLICATION_REQUISITE_PARKED;        break;
        case REVERSES:      r = MRP_APPLICATION_REQUISITE_REVERSES;      break;
        case BLINKER_LEFT:  r = MRP_APPLICATION_REQUISITE_BLINKER_LEFT;  break;
        case BLINKER_RIGHT: r = MRP_APPLICATION_REQUISITE_BLINKER_RIGHT; break;
        default:            luaL_error(L,"invalid requisite '%s'",name); break;
        }
    }

    return r;
}

static field_t field_check(lua_State *L, int idx, const char **ret_fldnam)
{
    const char *fldnam;
    size_t fldnamlen;
    field_t fldtyp;

    if ((fldnam = lua_tolstring(L, idx, &fldnamlen)))
        fldtyp = field_name_to_type(fldnam, fldnamlen);
    else {
        fldnam = "<invalid type>";
        fldtyp = 0;
    }

    if (ret_fldnam)
        *ret_fldnam = fldnam;

    return fldtyp;
}

static field_t field_name_to_type(const char *name, ssize_t len)
{
    if (len < 0)
        len = strlen(name);

    switch (len) {

    case 4:
        switch (name[0]) {
        case 'a':
            if (!strcmp(name, "area"))
                return AREA;
            break;
        case 'm':
            if (!strcmp(name, "mask"))
                return MASK;
            break;
        case 'n':
            if (!strcmp(name, "none"))
                return NONE;
            break;
        default:
            break;
        }
        break;

    case 5:
        if (!strcmp(name, "appid"))
            return APPID;
        if (!strcmp(name, "audio"))
            return AUDIO;
        break;

    case 6:
        switch (name[0]) {
        case 'p':
            if (!strcmp(name, "parked"))
                return PARKED;
            break;
        case 's':
            if (!strcmp(name, "screen"))
                return SCREEN;
            break;
        default:
            break;
        }
        break;

    case 7:
        if (!strcmp(name, "driving"))
            return DRIVING;
        break;

    case 8:
        if (!strcmp(name, "reverses"))
            return REVERSES;
        break;

    case 10:
        switch (name[0]) {
        case 'p':
            if (!strcmp(name, "privileges"))
                return PRIVILEGES;
            break;
        case 'r':
            if (!strcmp(name, "requisites"))
                return REQUISITES;
            break;
        default:
            break;
        }
        break;

    case 12:
        if (!strcmp(name, "blinker_left"))
            return BLINKER_LEFT;
        break;

    case 13:
        if (!strcmp(name, "blinker_right"))
            return BLINKER_RIGHT;
        break;

    case 14:
        if (!strcmp(name, "resource_class"))
            return RESOURCE_CLASS;
        break;

    case 15:
        if (!strcmp(name, "screen_priority"))
            return SCREEN_PRIORITY;
        break;

    default:
        break;
    }

    return 0;
}
