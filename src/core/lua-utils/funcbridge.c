/*
 * Copyright (c) 2012, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
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

#include <stdlib.h>
#include <string.h>


#include <lualib.h>
#include <lauxlib.h>

#include <murphy/common.h>

#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/core/lua-utils/object.h>

#define FUNCBRIDGE_METATABLE  "LuaBook.funcbridge"
#define USERDATA_METATABLE    "LuaBook.funcbridge.userdata"


static mrp_funcbridge_t *create_funcbridge(lua_State *, int, int);
static int call_from_lua(lua_State *);
static int get_funcbridge_field(lua_State *);
static int set_funcbridge_field(lua_State *);
static int funcbridge_destructor(lua_State *);


void mrp_create_funcbridge_class(lua_State *L)
{
    static const struct luaL_reg class_methods [] = {
        { NULL, NULL }
    };

    static const struct luaL_reg override_methods [] = {
        { "__call"    , call_from_lua        },
        { "__index"   , get_funcbridge_field },
        { "__newindex", set_funcbridge_field },
        {      NULL   ,     NULL             }
    };

    luaL_newmetatable(L, USERDATA_METATABLE);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -2);
    lua_settable(L, -3);        /* metatable.__index = metatable */
    lua_pushcfunction(L, funcbridge_destructor);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    luaL_newmetatable(L, FUNCBRIDGE_METATABLE);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -2);
    lua_settable(L, -3);        /* metatable.__index = metatable */
    luaL_openlib(L, NULL, override_methods, 0);
    lua_pop(L, 1);

    luaL_openlib(L, "builtin.method", class_methods, 0);
    lua_pushvalue(L, -2);
    lua_setmetatable(L, -2);
    lua_pop(L, 1);
}


mrp_funcbridge_t *mrp_funcbridge_create_cfunc(lua_State *L, const char *name,
                                              const char *signature,
                                              mrp_funcbridge_cfunc_t func,
                                              void *data)
{
    int builtin, top;
    mrp_funcbridge_t *fb;

    top = lua_gettop(L);

    if (luaL_findtable(L, LUA_GLOBALSINDEX, "builtin.method", 20))
        fb = NULL;
    else {
        builtin = lua_gettop(L);

        fb = create_funcbridge(L, 0, 1);

        fb->type = MRP_C_FUNCTION;
        fb->c.signature = strdup(signature);
        fb->c.func = func;
        fb->c.data = data;

        lua_pushstring(L, name);
        lua_pushvalue(L, -2);

        lua_rawset(L, builtin);

        lua_settop(L, top);
    }

    return fb;
}


mrp_funcbridge_t *mrp_funcbridge_create_luafunc(lua_State *L, int f)
{
    mrp_funcbridge_t *fb;

    if (f < 0 && f > LUA_REGISTRYINDEX)
        f = lua_gettop(L) + f + 1;

    switch (lua_type(L, f)) {

    case LUA_TTABLE:
        fb = mrp_funcbridge_check(L, f);
        break;

    case LUA_TFUNCTION:
        fb = create_funcbridge(L, 1, 1);
        lua_pushvalue(L, f);
        lua_rawseti(L, -2, 1);
        lua_pop(L, 1);
        fb->type = MRP_LUA_FUNCTION;
        break;

    default:
        luaL_argcheck(L, false, f < 0 ? lua_gettop(L) + f + 1 : f,
                      "'builtin.method.xxx' or lua function expected");
        fb = NULL;
        break;
    }

    return fb;
}


mrp_funcbridge_t *mrp_funcbridge_ref(lua_State *L, mrp_funcbridge_t *fb)
{
    if (fb->dead)
        return NULL;

    MRP_UNUSED(L);

    fb->refcnt++;

    return fb;
}

void mrp_funcbridge_unref(lua_State *L, mrp_funcbridge_t *fb)
{
    if (fb->refcnt > 1)
        fb->refcnt--;
    else {
        free((void *)fb->c.signature);
        fb->c.signature = NULL;

        if (fb->luatbl) {
            luaL_unref(L, LUA_REGISTRYINDEX, fb->luatbl);
            fb->luatbl = 0;
        }
    }
}

bool mrp_funcbridge_call_from_c(lua_State *L,
                                mrp_funcbridge_t *fb,
                                const char *signature,
                                mrp_funcbridge_value_t *args,
                                char *ret_type,
                                mrp_funcbridge_value_t *ret_value)
{
    char t;
    int i;
    int sp;
    mrp_funcbridge_value_t *a;
    int sts;
    bool success;

    if (!fb)
        success = false;
    else {
        switch (fb->type) {

        case MRP_C_FUNCTION:
            if (!strcmp(signature, fb->c.signature))
                success = fb->c.func(L, fb->c.data, signature, args, ret_type,
                                     ret_value);
            else {
                *ret_type = MRP_FUNCBRIDGE_STRING;
                ret_value->string = mrp_strdup("mismatching signature "
                                               "@ C invocation");
                success = false;
            }
            break;

        case MRP_LUA_FUNCTION:
            sp = lua_gettop(L);
            mrp_funcbridge_push(L, fb);
            lua_rawgeti(L, -1, 1);
            luaL_checktype(L, -1, LUA_TFUNCTION);
            for (i = 0;   (t = signature[i]);   i++) {
                a = args + i;
                switch (t) {
                case MRP_FUNCBRIDGE_STRING:
                    lua_pushstring(L, a->string);
                    break;
                case MRP_FUNCBRIDGE_INTEGER:
                    lua_pushinteger(L, a->integer);
                    break;
                case MRP_FUNCBRIDGE_FLOATING:
                    lua_pushnumber(L, a->floating);
                    break;
                case MRP_FUNCBRIDGE_OBJECT:
                    mrp_lua_push_object(L, a->pointer);
                    break;
                default:
                    success = false;
                    goto done;
                }
            }

            sts = lua_pcall(L, i, 1, 0);

            MRP_ASSERT(!sts || (sts && lua_type(L, -1) == LUA_TSTRING),
                       "lua pcall did not return error string when failed");

            switch (lua_type(L, -1)) {
            case LUA_TSTRING:
                *ret_type = MRP_FUNCBRIDGE_STRING;
                ret_value->string = mrp_strdup(lua_tolstring(L, -1, NULL));
                break;
            case LUA_TNUMBER:
                *ret_type = MRP_FUNCBRIDGE_FLOATING;
                ret_value->floating = lua_tonumber(L, -1);
                break;
            default:
                *ret_type = MRP_FUNCBRIDGE_NO_DATA;
                memset(ret_value, 0, sizeof(*ret_value));
                break;
            }
            success = !sts;
        done:
            lua_settop(L, sp);
            break;

        default:
            success = false;
            break;
        }
    }

    return success;
}

mrp_funcbridge_t *mrp_funcbridge_check(lua_State *L, int t)
{
    mrp_funcbridge_t *fb;

    luaL_checktype(L, t, LUA_TTABLE);

    lua_pushvalue(L, t);
    lua_pushliteral(L, "userdata");
    lua_rawget(L, -2);

    fb = (mrp_funcbridge_t *)luaL_checkudata(L, -1, USERDATA_METATABLE);
    luaL_argcheck(L, fb != NULL, 1, "'function bridge' expected");

    lua_pop(L, 2);

    return fb;
}


int mrp_funcbridge_push(lua_State *L, mrp_funcbridge_t *fb)
{
    if (!fb)
        lua_pushnil(L);
    else
        lua_rawgeti(L, LUA_REGISTRYINDEX, fb->luatbl);
    return 1;
}


static mrp_funcbridge_t *create_funcbridge(lua_State *L, int narr, int nrec)
{
    int table;
    mrp_funcbridge_t *fb;

    lua_createtable(L, narr, nrec);
    table = lua_gettop(L);

    luaL_getmetatable(L, FUNCBRIDGE_METATABLE);
    lua_setmetatable(L, table);

    lua_pushliteral(L, "userdata");

    fb = (mrp_funcbridge_t *)lua_newuserdata(L,sizeof(mrp_funcbridge_t));
    memset(fb, 0, sizeof(mrp_funcbridge_t));

    luaL_getmetatable(L, USERDATA_METATABLE);
    lua_setmetatable(L, -2);

    lua_rawset(L, table);

    lua_pushvalue(L, -1);
    fb->luatbl = luaL_ref(L, LUA_REGISTRYINDEX);

    fb->refcnt = 1;

    return fb;
}


static int call_from_lua(lua_State *L)
{
#define ARG_MAX 256

    mrp_funcbridge_t *fb = mrp_funcbridge_check(L, 1);
    int ret;
    int i, n, m, b, e;
    const char *s;
    char t;
    mrp_funcbridge_value_t args[ARG_MAX];
    mrp_funcbridge_value_t *a, r;

    b = 2;
    e = lua_gettop(L);
    n = e - b + 1;

    switch (fb->type) {

    case MRP_C_FUNCTION:
        m = strlen(fb->c.signature);

        if (n >= ARG_MAX - 1 || n > m)
            return luaL_error(L, "too many arguments");
        if (n < m)
            return luaL_error(L, "too few arguments");

        for (i = b, s = fb->c.signature, a= args;    i <= e;    i++, s++, a++){
            switch (*s) {
            case MRP_FUNCBRIDGE_STRING:
                a->string = luaL_checklstring(L, i, NULL);
                break;
            case MRP_FUNCBRIDGE_INTEGER:
                a->integer = luaL_checkinteger(L, i);
                break;
            case MRP_FUNCBRIDGE_FLOATING:
                a->floating = luaL_checknumber(L, i);
                break;
            case MRP_FUNCBRIDGE_OBJECT:
                a->pointer = mrp_lua_check_object(L, NULL, i);
                break;
            default:
                return luaL_error(L, "argument %d has unsupported type '%c'",
                                  (i - b) + 1, i);
            }
        }
        memset(a, 0, sizeof(*a));

        if (!fb->c.func(L, fb->c.data, fb->c.signature, args, &t, &r))
            return luaL_error(L, "c function invocation failed");

        switch (t) {
        case MRP_FUNCBRIDGE_NO_DATA:
            ret = 0;
            break;
        case MRP_FUNCBRIDGE_STRING:
            ret = 1;
            lua_pushstring(L, r.string);
            break;
        case MRP_FUNCBRIDGE_INTEGER:
            ret = 1;
            lua_pushinteger(L, r.integer);
            break;
        case MRP_FUNCBRIDGE_FLOATING:
            ret = 1;
            lua_pushnumber(L, r.floating);
            break;
        default:
            ret = 0;
            lua_pushnil(L);
        }
        break;

    case MRP_LUA_FUNCTION:
        lua_rawgeti(L, 1, 1);
        luaL_checktype(L, -1, LUA_TFUNCTION);
        lua_replace(L, 1);
        lua_call(L, n, 1);
        ret = 1;
        break;

    default:
        return luaL_error(L, "internal error");
    }

    return ret;

#undef ARG_MAX
}

static int get_funcbridge_field(lua_State *L)
{
    lua_pushnil(L);
    return 1;
}

static int set_funcbridge_field(lua_State *L)
{
    return luaL_error(L, "attempt to write a readonly object");
}

static int funcbridge_destructor(lua_State *L)
{
    mrp_funcbridge_t *fb;

    fb = (mrp_funcbridge_t *)luaL_checkudata(L, -1, USERDATA_METATABLE);

    if (!fb->dead) {
        fb->dead = true;
        mrp_funcbridge_unref(L, fb);
    }

    return 0;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
