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

#include "scripting-resource-client.h"
#include "wayland/scripting-wayland.h"

#define RESOURCE_CLIENT_CLASS   MRP_LUA_CLASS_SIMPLE(resource_client)

typedef struct scripting_resclnt_s  scripting_resclnt_t;
typedef struct funcbridge_def_s     funcbridge_def_t;

struct scripting_resclnt_s {
    mrp_resclnt_t *resclnt;
};

struct funcbridge_def_s {
    const char *name;
    const char *sign;
    mrp_funcbridge_cfunc_t func;
    void *data;
    mrp_funcbridge_t **ptr;
};

static int  resclnt_create(lua_State *);
static int  resclnt_getfield(lua_State *);
static int  resclnt_setfield(lua_State *);
static void resclnt_destroy(void *);

static scripting_resclnt_t *resclnt_check(lua_State *, int);

static mrp_resclnt_resource_set_type_t str_to_type(const char *);

static bool register_methods(lua_State *);


MRP_LUA_CLASS_DEF_SIMPLE (
    resource_client,               /* class name */
    scripting_resclnt_t,           /* userdata type */
    resclnt_destroy,               /* userdata destructor */
    MRP_LUA_METHOD_LIST (          /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR    (resclnt_create)
    ),
    MRP_LUA_METHOD_LIST (          /* overrides */
       MRP_LUA_OVERRIDE_CALL         (resclnt_create)
       MRP_LUA_OVERRIDE_GETFIELD     (resclnt_getfield)
       MRP_LUA_OVERRIDE_SETFIELD     (resclnt_setfield)
    )
);


static mrp_funcbridge_t *resource_set_create;
static mrp_funcbridge_t *resource_set_destroy;
static mrp_funcbridge_t *resource_set_acquire;
static mrp_funcbridge_t *resource_set_release;


void mrp_resclnt_scripting_init(lua_State *L)
{
    MRP_ASSERT(L, "invalid argument");

    mrp_lua_create_object_class(L, RESOURCE_CLIENT_CLASS);
    register_methods(L);
}


mrp_resclnt_t *mrp_resclnt_scripting_check(lua_State *L, int idx)
{
    scripting_resclnt_t *rc;
    mrp_resclnt_t *resclnt;

    if ((rc = resclnt_check(L, idx)) && (resclnt = rc->resclnt)) {
        MRP_ASSERT(resclnt->scripting_data == (void *)rc,
                   "confused with data structures");
        return resclnt;
    }

    return NULL;
}

mrp_resclnt_t *mrp_resclnt_scripting_unwrap(void *void_rc)
{
    scripting_resclnt_t *rc = (scripting_resclnt_t *)void_rc;

    if (rc && mrp_lua_get_object_classdef(rc) == RESOURCE_CLIENT_CLASS)
        return rc->resclnt;

    return NULL;
}

static int resclnt_create(lua_State *L)
{
    mrp_resclnt_t *resclnt;
    size_t fldnamlen;
    const char *fldnam;
    scripting_resclnt_t *rc;
    int table;

    MRP_LUA_ENTER;

    luaL_checktype(L, 2, LUA_TTABLE);

    rc = (scripting_resclnt_t *)mrp_lua_create_object(L, RESOURCE_CLIENT_CLASS,
                                                     NULL, 1);

    if (!rc)
        luaL_error(L, "failed to create resource client");

    table = lua_gettop(L);

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {
        switch (mrp_resclnt_scripting_field_name_to_type(fldnam, fldnamlen)) {

        default:
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_rawset(L, table);
            break;
        }
    }

    if (!(resclnt = mrp_resclnt_create()))
        luaL_error(L, "can't create resclnt object");

    rc->resclnt = resclnt;

    resclnt->scripting_data = rc;

    lua_settop(L, table);

    MRP_LUA_LEAVE(1);
}

static int resclnt_getfield(lua_State *L)
{
    scripting_resclnt_t *rc;
    const char *fldnam;
    mrp_resclnt_scripting_field_t fld;

    MRP_LUA_ENTER;

    fld = mrp_resclnt_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    rc = resclnt_check(L, 1);

    if (!rc)
        lua_pushnil(L);
    else {
        switch (fld) {

        case RESOURCE_SET_CREATE:
            mrp_funcbridge_push(L, resource_set_create);
            break;

        case RESOURCE_SET_DESTROY:
            mrp_funcbridge_push(L, resource_set_destroy);
            break;

        case RESOURCE_SET_ACQUIRE:
            mrp_funcbridge_push(L, resource_set_acquire);
            break;

        case RESOURCE_SET_RELEASE:
            mrp_funcbridge_push(L, resource_set_release);
            break;

        default:
            lua_pushstring(L, fldnam);
            lua_rawget(L, 1);
            break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  resclnt_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    resclnt_check(L, 1);
    luaL_error(L, "resource client is read-only");

    MRP_LUA_LEAVE(0);
}

static void resclnt_destroy(void *data)
{
    scripting_resclnt_t *rc = (scripting_resclnt_t *)data;

    MRP_UNUSED(data);
    MRP_UNUSED(rc);

    MRP_LUA_ENTER;

    MRP_LUA_LEAVE_NOARG;
}

static scripting_resclnt_t *resclnt_check(lua_State *L, int idx)
{

    return (scripting_resclnt_t*)mrp_lua_check_object(L, RESOURCE_CLIENT_CLASS,
                                                      idx);
}

static mrp_resclnt_resource_set_type_t str_to_type(const char *str)
{
    if (str) {
        if (!strcmp(str, "screen"))
            return MRP_RESCLIENT_RESOURCE_SET_SCREEN;
        else if (!strcmp(str, "audio"))
            return MRP_RESCLIENT_RESOURCE_SET_AUDIO;
        else if (!strcmp(str, "input"))
            return MRP_RESCLIENT_RESOURCE_SET_INPUT;
    }

    return MRP_RESCLIENT_RESOURCE_SET_UNKNOWN; /* == 0 */
}

static bool resource_set_create_bridge(lua_State *L,
                                       void *data,
                                       const char *signature,
                                       mrp_funcbridge_value_t *args,
                                       char *ret_type,
                                       mrp_funcbridge_value_t *ret_val)
{
    mrp_resclnt_t *resclnt;
    mrp_resclnt_resource_set_type_t type;
    const char *zone;
    const char *appid;
    int32_t objid;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;

    if (strcmp(signature, "osssd")) {
        mrp_log_error("bad signature: expected 'osssd' got '%s'", signature);
        return false;
    }

    if (!(resclnt = mrp_resclnt_scripting_unwrap(args[0].pointer))) {
        mrp_log_error("argument 1 is not a 'resource_client' class object");
        return false;
    }

    if (!(type = str_to_type(args[1].string))) {
        mrp_log_error("argument 2 is not a valid resource type string");
        return false;
    }

    zone  = args[2].string;
    appid = args[3].string;
    objid = args[4].integer;

    mrp_resclnt_add_resource_set(resclnt, type, zone, appid, NULL + objid);

    return true;
}


static bool resource_set_destroy_bridge(lua_State *L,
                                        void *data,
                                        const char *signature,
                                        mrp_funcbridge_value_t *args,
                                        char *ret_type,
                                        mrp_funcbridge_value_t *ret_val)
{
    mrp_resclnt_t *resclnt;
    mrp_resclnt_resource_set_type_t type;
    int32_t objid;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;

    if (strcmp(signature, "osd")) {
        mrp_log_error("bad signature: expected 'osd' got '%s'", signature);
        return false;
    }

    if (!(resclnt = mrp_resclnt_scripting_unwrap(args[0].pointer))) {
        mrp_log_error("argument 1 is not a 'resource_client' class object");
        return false;
    }

    if (!(type = str_to_type(args[1].string))) {
        mrp_log_error("argument 2 is not a valid resource type string");
        return false;
    }

    objid = args[2].integer;

    mrp_resclnt_remove_resource_set(resclnt, type, NULL + objid);

    return true;
}


static bool resource_set_acquire_bridge(lua_State *L,
                                        void *data,
                                        const char *signature,
                                        mrp_funcbridge_value_t *args,
                                        char *ret_type,
                                        mrp_funcbridge_value_t *ret_val)
{
    mrp_resclnt_t *resclnt;
    mrp_resclnt_resource_set_type_t type;
    int32_t objid;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;

    if (strcmp(signature, "osd")) {
        mrp_log_error("bad signature: expected 'osd' got '%s'", signature);
        return false;
    }

    if (!(resclnt = mrp_resclnt_scripting_unwrap(args[0].pointer))) {
        mrp_log_error("argument 1 is not a 'resource_client' class object");
        return false;
    }

    if (!(type = str_to_type(args[1].string))) {
        mrp_log_error("argument 2 is not a valid resource type string");
        return false;
    }

    objid = args[2].integer;

    mrp_resclnt_acquire_resource_set(resclnt, type, NULL + objid);

    return true;
}


static bool resource_set_release_bridge(lua_State *L,
                                        void *data,
                                        const char *signature,
                                        mrp_funcbridge_value_t *args,
                                        char *ret_type,
                                        mrp_funcbridge_value_t *ret_val)
{
    mrp_resclnt_t *resclnt;
    mrp_resclnt_resource_set_type_t type;
    int32_t objid;

    MRP_UNUSED(L);
    MRP_UNUSED(data);
    MRP_UNUSED(ret_val);
    MRP_ASSERT(signature && args && ret_type, "invalid argument");

    *ret_type = MRP_FUNCBRIDGE_NO_DATA;

    if (strcmp(signature, "osd")) {
        mrp_log_error("bad signature: expected 'osd' got '%s'", signature);
        return false;
    }

    if (!(resclnt = mrp_resclnt_scripting_unwrap(args[0].pointer))) {
        mrp_log_error("argument 1 is not a 'resource_client' class object");
        return false;
    }

    if (!(type = str_to_type(args[1].string))) {
        mrp_log_error("argument 2 is not a valid resource type string");
        return false;
    }

    objid = args[2].integer;

    mrp_resclnt_release_resource_set(resclnt, type, NULL + objid);

    return true;
}


static bool register_methods(lua_State *L)
{
#define FUNCBRIDGE(n,s,d) { #n, s, n##_bridge, d, &n }
#define FUNCBRIDGE_END    { NULL, NULL, NULL, NULL, NULL }

    static funcbridge_def_t funcbridge_defs[] = {
        FUNCBRIDGE(resource_set_create   , "osssd", NULL),
        FUNCBRIDGE(resource_set_destroy  , "osd"  , NULL),
        FUNCBRIDGE(resource_set_acquire  , "osd"  , NULL),
        FUNCBRIDGE(resource_set_release  , "osd"  , NULL),
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


mrp_resclnt_scripting_field_t
mrp_resclnt_scripting_field_check(lua_State *L,int idx,const char **ret_fldnam)
{
    const char *fldnam;
    size_t fldnamlen;
    mrp_resclnt_scripting_field_t fldtyp;

    if (!(fldnam = lua_tolstring(L, idx, &fldnamlen)))
        fldtyp = 0;
    else
        fldtyp = mrp_resclnt_scripting_field_name_to_type(fldnam, fldnamlen);

    if (ret_fldnam)
        *ret_fldnam = fldnam;

    return fldtyp;
}

mrp_resclnt_scripting_field_t
mrp_resclnt_scripting_field_name_to_type(const char *name, ssize_t len)
{
    if (len < 0)
        len = strlen(name);

    switch (len) {

    case 19:
        if (!strcmp(name, "resource_set_create"))
            return RESOURCE_SET_CREATE;
        break;

    case 20:
        switch (name[13]) {
        case 'a':
            if (!strcmp(name, "resource_set_acquire"))
                return RESOURCE_SET_ACQUIRE;
            break;
        case 'd':
            if (!strcmp(name, "resource_set_destroy"))
                return RESOURCE_SET_DESTROY;
            break;
        case 'r':
            if (!strcmp(name, "resource_set_release"))
                return RESOURCE_SET_RELEASE;
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }

    return 0;
}
