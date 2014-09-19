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


#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>

#include <murphy/common.h>
#include <murphy/core.h>
#include <murphy/common/dbus-libdbus.h>
#include <murphy/common/process.h>

#include <murphy-db/mdb.h>
#include <murphy-db/mqi.h>
#include <murphy-db/mql.h>

#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-bindings/murphy.h>
#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/core/lua-decision/mdb.h>
#include <murphy/core/lua-decision/element.h>

enum {
    ARG_AMB_DBUS_ADDRESS,
    ARG_AMB_DBUS_BUS,
    ARG_AMB_CONFIG_FILE,
    ARG_AMB_ID,
    ARG_AMB_TPORT_ADDRESS,
    ARG_AMB_STARTUP_DELAY,
    ARG_AMB_FORCE_SUBSCRIPTION,
};

enum amb_type {
    amb_string = 's',
    amb_bool   = 'b',
    amb_int32  = 'i',
    amb_int16  = 'n',
    amb_uint32 = 'u',
    amb_uint16 = 'q',
    amb_byte   = 'y',
    amb_double = 'd',
};

#define AMB_NAME                "name"
#define AMB_HANDLER             "handler"
#define AMB_DBUS_DATA           "dbus_data"
#define AMB_OBJECT              "obj"
#define AMB_INTERFACE           "interface"
#define AMB_MEMBER              "property"
#define AMB_OBJECTNAME          "objectname"
#define AMB_SIGNATURE           "signature"
#define AMB_BASIC_TABLE_NAME    "basic_table_name"
#define AMB_OUTPUTS             "outputs"
#define AMB_STATE_TABLE_NAME    "amb_state"

/*
signal sender=:1.35 -> dest=(null destination) serial=716 path=/c0ffee8ac6054a06903459c1deadbeef/0/Transmission; interface=org.automotive.Transmission; member=GearPositionChanged
   variant       int32 6
   double 1.38978e+09
signal sender=:1.35 -> dest=(null destination) serial=717 path=/c0ffee8ac6054a06903459c1deadbeef/0/Transmission; interface=org.freedesktop.DBus.Properties; member=PropertiesChanged
   string "org.automotive.Transmission"
   array [
      dict entry(
         string "GearPosition"
         variant             int32 6
      )
      dict entry(
         string "GearPositionSequence"
         variant             int32 -1
      )
      dict entry(
         string "Time"
         variant             double 1.38978e+09
      )
   ]
   array [
   ]

signal sender=:1.35 -> dest=(null destination) serial=718 path=/c0ffee8ac6054a06903459c1deadbeef/0/VehicleSpeed; interface=org.automotive.VehicleSpeed; member=VehicleSpeedChanged
   variant       uint16 0
   double 1.38978e+09
signal sender=:1.35 -> dest=(null destination) serial=719 path=/c0ffee8ac6054a06903459c1deadbeef/0/VehicleSpeed; interface=org.freedesktop.DBus.Properties; member=PropertiesChanged
   string "org.automotive.VehicleSpeed"
   array [
      dict entry(
         string "VehicleSpeed"
         variant             uint16 0
      )
      dict entry(
         string "VehicleSpeedSequence"
         variant             int32 -1
      )
      dict entry(
         string "Time"
         variant             double 1.38978e+09
      )
   ]
   array [
   ]
*/

typedef struct {
    char *name;
    mrp_dbus_type_t type;
    union {
        int32_t i;
        uint32_t u;
        double f;
        char *s;
    } value;
    bool initialized;
} dbus_basic_property_t;

typedef void (*property_updated_cb_t)(dbus_basic_property_t *property,
        void *user_data);

typedef struct {
    struct {
        char *obj;
        char *iface;
        char *name;            /* property name ("GearPosition") */
        char *objectname;      /* amb object to query ("Transmission") */
        char *signature;
        bool undefined_object_path;
    } dbus_data;
    char *name;
    char *basic_table_name;
    int handler_ref;
    int outputs_ref;
} lua_amb_property_t;

typedef struct {
    mrp_dbus_t *dbus;
    const char *amb_addr;
    const char *config_file;
    const char *amb_id;
    const char *tport_addr;
    lua_State *L;
    mrp_list_hook_t lua_properties;
    mrp_htbl_t *dbus_property_objects; /* path to dbus_property_object_t */

    mrp_mainloop_t *ml;

    mrp_transport_t *lt;
    mrp_transport_t *t;
} data_t;

typedef struct {
    mqi_column_def_t defs[4];

    mql_statement_t *update_operation;
    mqi_data_type_t type;
    mqi_handle_t table;
} basic_table_data_t;

typedef struct {
    /* PropertiesChanged signals are received by the object and may contain
     * several properties that are changed. Hence we'll need to represent that
     * with a custom datatype. */

    /* htbl backpointer for freeing */
    char *path;

    /* list of properties that belong to this object */
    mrp_htbl_t *dbus_properties; /* interface+property to dbus_property_watch_t */

    mrp_list_hook_t hook;
} dbus_property_object_t;

typedef struct {
    dbus_basic_property_t prop;
    dbus_property_object_t *o; /* top level */

    property_updated_cb_t cb;
    void *user_data;

    lua_amb_property_t *lua_prop;

    /* for basic tables that we manage ourselves */
    basic_table_data_t *tdata;

    /* htbl backpointer for freeing */
    char *key;

    mrp_list_hook_t hook;
    data_t *ctx;
} dbus_property_watch_t;

static data_t *global_ctx = NULL;

static basic_table_data_t *create_basic_property_table(const char *table_name,
        const char *member, int type);
static void delete_basic_table_data(basic_table_data_t *tdata);
static int find_property_object(data_t *ctx, dbus_property_watch_t *w,
        const char *prop);
static int subscribe_property(data_t *ctx, dbus_property_watch_t *w);
static void basic_property_updated(dbus_basic_property_t *prop, void *userdata);
static int create_transport(mrp_mainloop_t *ml, data_t *ctx);
static int property_signal_handler(mrp_dbus_t *dbus, mrp_dbus_msg_t *msg,
        void *data);

/* Lua config */

static int amb_constructor(lua_State *L);
static int amb_setfield(lua_State *L);
static int amb_getfield(lua_State *L);
static void lua_amb_destroy(void *data);

#define PROPERTY_CLASS MRP_LUA_CLASS(amb, property)

MRP_LUA_METHOD_LIST_TABLE (
    amb_methods,          /* methodlist name */
    MRP_LUA_METHOD_CONSTRUCTOR  (amb_constructor)
);

MRP_LUA_METHOD_LIST_TABLE (
    amb_overrides,     /* methodlist name */
    MRP_LUA_OVERRIDE_CALL       (amb_constructor)
    MRP_LUA_OVERRIDE_GETFIELD   (amb_getfield)
    MRP_LUA_OVERRIDE_SETFIELD   (amb_setfield)
);

MRP_LUA_CLASS_DEF (
    amb,                /* main class name */
    property,           /* constructor name */
    lua_amb_property_t, /* userdata type */
    lua_amb_destroy,    /* userdata destructor */
    amb_methods,        /* class methods */
    amb_overrides       /* class overrides */
);



static void lua_amb_destroy(void *data)
{
    lua_amb_property_t *prop = (lua_amb_property_t *)data;

    MRP_LUA_ENTER;

    mrp_log_info("AMB: lua_amb_destroy");

    MRP_UNUSED(prop);

    MRP_LUA_LEAVE_NOARG;
}


static int count_keys_cb(void *key, void *object, void *user_data)
{
    int *count = user_data;

    MRP_UNUSED(key);
    MRP_UNUSED(object);

    *count = *count + 1;

    return MRP_HTBL_ITER_MORE;
}


static void destroy_prop(data_t *ctx, dbus_property_watch_t *w)
{
    dbus_property_object_t *o = w->o;
    int len = 0;

    /* remove property from the object */

    mrp_htbl_remove(o->dbus_properties, w->key, FALSE);

    /* check if no-one is interested in the signals o receives */

    mrp_htbl_foreach(o->dbus_properties, count_keys_cb, &len);

    if (len == 0) {
        if (ctx->dbus) {
            mrp_dbus_unsubscribe_signal(ctx->dbus, property_signal_handler, o,
                    NULL, o->path,
                    "org.freedesktop.DBus.Properties",
                    "PropertiesChanged", NULL);
        }
        if (ctx->dbus_property_objects) {
           mrp_htbl_remove(ctx->dbus_property_objects, o->path, TRUE);
        }
    }

    mrp_free(w->key);

    /* delete the table data */

    if (w->tdata)
        delete_basic_table_data(w->tdata);

    if (w->lua_prop) {
        /* TODO */
        char *name = w->lua_prop->name;
        mrp_free(w->lua_prop->basic_table_name);
        mrp_free(w->lua_prop->dbus_data.iface);
        mrp_free(w->lua_prop->dbus_data.name);
        mrp_free(w->lua_prop->dbus_data.obj);
        mrp_free(w->lua_prop->dbus_data.objectname);
        mrp_free(w->lua_prop->dbus_data.signature);

        mrp_lua_destroy_object(ctx->L, name, 0, w->lua_prop);

        mrp_free(name);
    }

    mrp_free(w);
}


static int amb_constructor(lua_State *L)
{
    lua_amb_property_t *prop;
    size_t field_name_len = 0;
    const char *field_name;
    data_t *ctx = global_ctx;
    dbus_property_watch_t *w = NULL;
    char *error = "unknown error";

    MRP_LUA_ENTER;

    mrp_debug("AMB: amb_constructor, stack size: %d", lua_gettop(L));

    prop = (lua_amb_property_t *)
            mrp_lua_create_object(L, PROPERTY_CLASS, NULL, 0);

    if (!prop)
        goto error;

    prop->handler_ref = LUA_NOREF;
    prop->outputs_ref = LUA_NOREF;

    MRP_LUA_FOREACH_FIELD(L, 2, field_name, field_name_len) {
        char buf[field_name_len+1];

        strncpy(buf, field_name, field_name_len);
        buf[field_name_len] = '\0';

        /* mrp_log_info("field name: %s", buf); */

        if (strncmp(field_name, "dbus_data", field_name_len) == 0) {

            luaL_checktype(L, -1, LUA_TTABLE);

            lua_pushnil(L);

            while (lua_next(L, -2)) {

                const char *key;
                const char *value;

                luaL_checktype(L, -2, LUA_TSTRING);
                luaL_checktype(L, -1, LUA_TSTRING);

                key = lua_tostring(L, -2);
                value = lua_tostring(L, -1);

                mrp_log_info("%s -> %s", key, value);

                if (!key || !value) {
                    error = "key or value undefined";
                    goto error;
                }

                if (strcmp(key, AMB_SIGNATURE) == 0) {
                    prop->dbus_data.signature = mrp_strdup(value);
                }
                else if (strcmp(key, AMB_MEMBER) == 0) {
                    prop->dbus_data.name = mrp_strdup(value);
                }
                else if (strcmp(key, AMB_OBJECTNAME) == 0) {
                    prop->dbus_data.objectname = mrp_strdup(value);
                }
                else if (strcmp(key, AMB_OBJECT) == 0) {
                    if (strcmp(value, "undefined") == 0) {
                        /* need to query AMB for finding the object path */
                        mrp_log_info("querying AMB for correct path");
                        prop->dbus_data.undefined_object_path = TRUE;
                        prop->dbus_data.obj = NULL;
                    }
                    else {
                        prop->dbus_data.undefined_object_path = FALSE;
                        prop->dbus_data.obj = mrp_strdup(value);
                    }
                }
                else if (strcmp(key, AMB_INTERFACE) == 0) {
                    prop->dbus_data.iface = mrp_strdup(value);
                }
                else {
                    error = "unknown key";
                    goto error;
                }

                lua_pop(L, 1);
            }

            if (prop->dbus_data.objectname == NULL)
                prop->dbus_data.objectname = mrp_strdup(prop->dbus_data.name);

            /* check that we have all necessary data */
            if (prop->dbus_data.signature == NULL ||
                prop->dbus_data.iface == NULL ||
                prop->dbus_data.name == NULL ||
                prop->dbus_data.objectname == NULL ||
                (!prop->dbus_data.undefined_object_path &&
                 prop->dbus_data.obj == NULL)) {
                error = "missing data";
                goto error;
            }
        }
        else if (strncmp(field_name, "handler", field_name_len) == 0) {
            luaL_checktype(L, -1, LUA_TFUNCTION);
            prop->handler_ref = luaL_ref(L, LUA_REGISTRYINDEX);
            lua_pushnil(L); /* need two items on the stack */
        }
        else if (strncmp(field_name, AMB_NAME, field_name_len) == 0) {
            luaL_checktype(L, -1, LUA_TSTRING);
            prop->name = mrp_strdup(lua_tostring(L, -1));
            mrp_lua_set_object_name(L, PROPERTY_CLASS, prop->name);
        }
        else if (strncmp(field_name, "basic_table_name", field_name_len) == 0) {
            luaL_checktype(L, -1, LUA_TSTRING);
            prop->basic_table_name = mrp_strdup(lua_tostring(L, -1));
        }
        else if (strncmp(field_name, AMB_OUTPUTS, field_name_len) == 0) {
            prop->outputs_ref = luaL_ref(L, LUA_REGISTRYINDEX);
            lua_pushnil(L); /* need two items on the stack */
        }
    }

    if (!prop->name) {
        error = "missing property name";
        goto error;
    }

    if (prop->handler_ref == LUA_NOREF && !prop->basic_table_name) {
        error = "missing table name";
        goto error;
    }

    w = (dbus_property_watch_t *) mrp_allocz(sizeof(dbus_property_watch_t));

    if (!w) {
        error = "out of memory";
        goto error;
    }

    w->ctx = ctx;
    w->lua_prop = prop;
    w->prop.initialized = FALSE;
    w->prop.type = MRP_DBUS_TYPE_INVALID;
    w->prop.name = mrp_strdup(w->lua_prop->dbus_data.name);

    if (!w->prop.name) {
        error = "missing watch property name";
        goto error;
    }

    if (prop->handler_ref == LUA_NOREF) {
        basic_table_data_t *tdata;

        w->prop.type = w->lua_prop->dbus_data.signature[0]; /* FIXME */

        tdata = create_basic_property_table(prop->basic_table_name,
            prop->dbus_data.name, w->prop.type);

        if (!tdata) {
            error = "could not create table data";
            goto error;
        }

        w->tdata = tdata;

        w->cb = basic_property_updated;
        w->user_data = w;
    }

    mrp_list_init(&w->hook);

    mrp_list_append(&ctx->lua_properties, &w->hook);

    /* TODO: need some mapping? or custom property_watch? */

    /* TODO: put the object to a global table or not? maybe better to just
     * unload them when the plugin is unloaded. */

    mrp_lua_push_object(L, prop);

    MRP_LUA_LEAVE(1);

error:
    if (w)
        destroy_prop(global_ctx, w);

    mrp_log_error("AMB: amb_constructor error: %s", error);
    MRP_LUA_LEAVE(0);
}

static int amb_getfield(lua_State *L)
{
    lua_amb_property_t *prop = (lua_amb_property_t *)
            mrp_lua_check_object(L, PROPERTY_CLASS, 1);
    size_t field_name_len;
    const char *field_name = lua_tolstring(L, 2, &field_name_len);

    MRP_LUA_ENTER;

    if (!prop)
        goto error;

    mrp_debug("AMB: amb_getfield");

    if (strncmp(field_name, AMB_NAME, field_name_len) == 0) {
        if (prop->name)
            lua_pushstring(L, prop->name);
        else
            goto error;
    }
    else if (strncmp(field_name, AMB_HANDLER, field_name_len) == 0) {
        if (prop->handler_ref != LUA_NOREF)
            lua_rawgeti(L, LUA_REGISTRYINDEX, prop->handler_ref);
        else
            goto error;
    }
    else if (strncmp(field_name, AMB_DBUS_DATA, field_name_len) == 0) {
        lua_newtable(L);

        lua_pushstring(L, AMB_OBJECT);
        if (prop->dbus_data.obj)
            lua_pushstring(L, prop->dbus_data.obj);
        else
            lua_pushstring(L, "undefined");
        lua_settable(L, -3);

        lua_pushstring(L, AMB_INTERFACE);
        lua_pushstring(L, prop->dbus_data.iface);
        lua_settable(L, -3);

        lua_pushstring(L, AMB_MEMBER);
        lua_pushstring(L, prop->dbus_data.name);
        lua_settable(L, -3);
        lua_pushstring(L, AMB_OBJECTNAME);
        lua_pushstring(L, prop->dbus_data.objectname);
        lua_settable(L, -3);

        lua_pushstring(L, AMB_SIGNATURE);
        lua_pushstring(L, prop->dbus_data.signature);
        lua_settable(L, -3);
    }
    else if (strncmp(field_name, "basic_table_name", field_name_len) == 0) {
        if (prop->basic_table_name)
            lua_pushstring(L, prop->basic_table_name);
        else
            goto error;
    }
    else if (strncmp(field_name, AMB_OUTPUTS, field_name_len) == 0) {
        if (prop->outputs_ref != LUA_NOREF)
            lua_rawgeti(L, LUA_REGISTRYINDEX, prop->outputs_ref);
        else
            goto error;
    }
    else {
        goto error;
    }

    MRP_LUA_LEAVE(1);

error:
    lua_pushnil(L);
    MRP_LUA_LEAVE(1);
}

static int amb_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    MRP_UNUSED(L);

    mrp_debug("AMB: amb_setfield");

    MRP_LUA_LEAVE(0);
}

/* lua config end */

static bool parse_elementary_value(lua_State *L,
        mrp_dbus_msg_t *msg, dbus_property_watch_t *w)
{
    int32_t i32_val;
    int16_t i16_val;
    uint32_t u32_val;
    uint16_t u16_val;
    uint8_t byte_val;
    double d_val;
    char *s_val;

    mrp_dbus_type_t sig;

    MRP_UNUSED(w);

    sig = mrp_dbus_msg_arg_type(msg, NULL);

    switch (sig) {
        case MRP_DBUS_TYPE_INT32:
            mrp_dbus_msg_read_basic(msg, sig, &i32_val);
            lua_pushinteger(L, i32_val);
            break;
        case MRP_DBUS_TYPE_INT16:
            mrp_dbus_msg_read_basic(msg, sig, &i16_val);
            lua_pushinteger(L, i16_val);
            break;
        case MRP_DBUS_TYPE_UINT32:
            mrp_dbus_msg_read_basic(msg, sig, &u32_val);
            lua_pushinteger(L, u32_val);
            break;
        case MRP_DBUS_TYPE_UINT16:
            mrp_dbus_msg_read_basic(msg, sig, &u16_val);
            lua_pushinteger(L, u16_val);
            break;
        case MRP_DBUS_TYPE_BOOLEAN:
            mrp_dbus_msg_read_basic(msg, sig, &u32_val);
            lua_pushboolean(L, u32_val == TRUE);
            break;
        case MRP_DBUS_TYPE_BYTE:
            mrp_dbus_msg_read_basic(msg, sig, &byte_val);
            lua_pushinteger(L, byte_val);
            break;
        case MRP_DBUS_TYPE_DOUBLE:
            mrp_dbus_msg_read_basic(msg, sig, &d_val);
            lua_pushnumber(L, d_val);
            break;
        case MRP_DBUS_TYPE_STRING:
            mrp_dbus_msg_read_basic(msg, sig, &s_val);
            lua_pushstring(L, s_val);
            break;
        default:
            mrp_log_error("AMB: parse_elementary_value: unknown type");
            goto error;
    }

    return TRUE;

error:
    return FALSE;
}

static bool parse_value(lua_State *L, mrp_dbus_msg_t *msg,
        dbus_property_watch_t *w);

static bool parse_struct(lua_State *L,
        mrp_dbus_msg_t *msg, dbus_property_watch_t *w)
{
    int i = 1;

    /* initialize the table */
    lua_newtable(L);

    mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_STRUCT, NULL);

    while (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_INVALID) {

        /* struct "index" */
        lua_pushinteger(L, i++);

        parse_value(L, msg, w);

        /* put the values to the table */
        lua_settable(L, -3);
    }

    mrp_dbus_msg_exit_container(msg);

    return TRUE;
}


static bool parse_dict_entry(lua_State *L,
        mrp_dbus_msg_t *msg, dbus_property_watch_t *w)
{
    mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_DICT_ENTRY, NULL);

    while (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_INVALID) {

        /* key must be elementary, value can be anything */
        parse_elementary_value(L, msg, w);
        parse_value(L, msg, w);

        /* put the values to the table */
        lua_settable(L, -3);
    }

    mrp_dbus_msg_exit_container(msg); /* dict entry */

    return TRUE;
}

static bool parse_array(lua_State *L,
        mrp_dbus_msg_t *msg, dbus_property_watch_t *w)
{
    /* the lua array */
    lua_newtable(L);

    mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_ARRAY, NULL);

    /* the problem: if the value inside array is a dict entry, the
     * indexing of elements need to be done with dict keys instead
     * of numbers. */

    if (mrp_dbus_msg_arg_type(msg, NULL) == MRP_DBUS_TYPE_DICT_ENTRY) {
        while (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_INVALID) {
            parse_dict_entry(L, msg, w);
        }
    }
    else {
        int i = 1;

        while (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_INVALID) {

            /* array index */
            lua_pushinteger(L, i++);

            parse_value(L, msg, w);

            /* put the values to the table */
            lua_settable(L, -3);
        }
    }

    mrp_dbus_msg_exit_container(msg); /* array */

    return TRUE;
}

static bool parse_value(lua_State *L, mrp_dbus_msg_t *msg,
        dbus_property_watch_t *w)
{
    mrp_dbus_type_t curr;

    curr = mrp_dbus_msg_arg_type(msg, NULL);

    switch (curr) {
        case MRP_DBUS_TYPE_BYTE:
        case MRP_DBUS_TYPE_BOOLEAN:
        case MRP_DBUS_TYPE_INT16:
        case MRP_DBUS_TYPE_INT32:
        case MRP_DBUS_TYPE_UINT16:
        case MRP_DBUS_TYPE_UINT32:
        case MRP_DBUS_TYPE_DOUBLE:
        case MRP_DBUS_TYPE_STRING:
            return parse_elementary_value(L, msg, w);
        case MRP_DBUS_TYPE_ARRAY:
            return parse_array(L, msg, w);
        case MRP_DBUS_TYPE_STRUCT:
            return parse_struct(L, msg, w);
        case MRP_DBUS_TYPE_DICT_ENTRY:
            goto error; /* these are handled from parse_array */
        case MRP_DBUS_TYPE_INVALID:
            return TRUE;
        default:
            break;
    }

error:
    mrp_log_error("AMB: failed to parse D-Bus property (sig[i] %c)", curr);
    return FALSE;
}

static void lua_property_handler(mrp_dbus_msg_t *msg, dbus_property_watch_t *w)
{
#if 0
    char *variant_sig = NULL;
#endif

    if (!w || !msg) {
        mrp_log_error("AMB: no dbus property watch set");
        goto end;
    }

    if (w->lua_prop->handler_ref == LUA_NOREF) {
        mrp_log_error("AMB: no lua reference");
        goto end;
    }

    if (mrp_dbus_msg_type(msg) == MRP_DBUS_MESSAGE_TYPE_ERROR) {
        mrp_log_error("AMB: error message from ambd");
        goto end;
    }

    if (!mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_VARIANT, NULL)) {
        mrp_log_error("AMB: message parameter wasn't a variant");
        goto end;
    }

    /*
    mrp_log_info("iter sig: %s, expected: %s",
            variant_sig, w->lua_prop->dbus_data.signature);
    */


#if 0
    /* FIXME: check this when the D-Bus API has support */
    variant_sig = dbus_message_iter_get_signature(&variant_iter);

    if (!variant_sig) {
        mrp_log_error("amb: could not get variant signature");
        goto error;
    }

    /* check if we got what we were expecting */
    if (strcmp(variant_sig, w->lua_prop->dbus_data.signature) != 0) {
        mrp_log_error("amb: dbus data signature didn't match");
        goto error;
    }
#endif

    /* load the function pointer to the stack */
    lua_rawgeti(w->ctx->L, LUA_REGISTRYINDEX, w->lua_prop->handler_ref);

    /* "self" parameter */
    mrp_lua_push_object(w->ctx->L, w->lua_prop);

    /* parse values to the stack */
    parse_value(w->ctx->L, msg, w);

    /* call the handler function */
    lua_pcall(w->ctx->L, 2, 0, 0);

    mrp_dbus_msg_exit_container(msg);

end:
    /* TODO: clean up the variant sig string */
    return;
}


static void basic_property_handler(mrp_dbus_msg_t *msg, dbus_property_watch_t *w)
{
    int32_t i32_val;
    int16_t i16_val;
    uint32_t u32_val;
    uint16_t u16_val;
    uint8_t byte_val;
    double d_val;
    char *s_val;

    if (!w || !msg) {
        mrp_log_error("AMB: no dbus property watch set");
        goto error;
    }

    if (!mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_VARIANT, NULL)) {
        mrp_log_error("AMB: message parameter wasn't a variant");
        goto error;
    }

    /* check that D-Bus type matches the expected type */

    if (mrp_dbus_msg_arg_type(msg, NULL) != w->prop.type)  {
        mrp_log_error("AMB: argument type %c did not match expected type %c",
                mrp_dbus_msg_arg_type(msg, NULL), w->prop.type);
        goto error;
    }

    switch (w->prop.type) {
        case MRP_DBUS_TYPE_INT32:
            mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_INT32, &i32_val);
            w->prop.value.i = i32_val;
            break;
        case MRP_DBUS_TYPE_INT16:
            mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_INT16, &i16_val);
            w->prop.value.i = i16_val;
            break;
        case MRP_DBUS_TYPE_UINT32:
            mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_UINT32, &u32_val);
            w->prop.value.u = u32_val;
            break;
        case MRP_DBUS_TYPE_UINT16:
            mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_UINT16, &u16_val);
            w->prop.value.u = u16_val;
            break;
        case MRP_DBUS_TYPE_BOOLEAN:
            mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_BOOLEAN, &u32_val);
            w->prop.value.u = u32_val;
            break;
        case MRP_DBUS_TYPE_BYTE:
            mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_BYTE, &byte_val);
            w->prop.value.u = byte_val;
            break;
        case MRP_DBUS_TYPE_DOUBLE:
            mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_DOUBLE, &d_val);
            w->prop.value.f = d_val;
            break;
        case MRP_DBUS_TYPE_STRING:
            mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, &s_val);
            w->prop.value.s = mrp_strdup(s_val);
            break;
        default:
            mrp_dbus_msg_exit_container(msg);
            goto error;
    }

    mrp_dbus_msg_exit_container(msg);

    if (w->cb)
        w->cb(&w->prop, w->user_data);

    return;

error:
    mrp_log_error("AMB: failed to parse property value");
    return;
}

static int property_signal_handler(mrp_dbus_t *dbus, mrp_dbus_msg_t *msg,
        void *data)
{
    dbus_property_object_t *o = (dbus_property_object_t *) data;
    dbus_property_watch_t *w;
    char *interface;

    MRP_UNUSED(dbus);

    if (!msg) {
        mrp_log_error("AMB: message is NULL");
        return TRUE;
    }

    if (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_STRING)  {
        mrp_log_error("AMB: argument type %c , expected interface string",
                mrp_dbus_msg_arg_type(msg, NULL));
        return TRUE;
    }

    mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, &interface);

    /* loop for all the properties */
    mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_ARRAY, NULL);

    while (mrp_dbus_msg_arg_type(msg, NULL) == MRP_DBUS_TYPE_DICT_ENTRY) {
        char *property_name;

        mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_DICT_ENTRY, NULL);

        if (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_STRING)  {
            mrp_log_error("AMB: argument type %c, expected property name",
                mrp_dbus_msg_arg_type(msg, NULL));
            return TRUE;
        }

        mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, &property_name);

        /* the right handler for the property is now defined by
         * property_name and interface */

        if (interface && property_name) {
            int interface_len = strlen(interface);
            int property_name_len = strlen(property_name);
            char key[interface_len + 1 + property_name_len + 1];

            strncpy(key, interface, interface_len);
            *(key+interface_len) = '-';
            strncpy(key+interface_len+1, property_name, property_name_len);
            key[interface_len + 1 + property_name_len] = '\0';

            mrp_debug("AMB: looking up property watch from %p with key '%s'",
                    o, key);

            /* find the right dbus_property_watch */
            w = mrp_htbl_lookup(o->dbus_properties, key);

            if (w) {
                /* we are interested in this property of this object */
                mrp_debug("AMB: PropertiesChanged for %s; %s handling",
                        property_name, w->tdata ? "basic" : "lua");

                /* process the variant from the dict */
                if (w->tdata) {
                    basic_property_handler(msg, w);
                }
                else {
                    lua_property_handler(msg, w);
                }
            }
        }
        mrp_dbus_msg_exit_container(msg); /* dict entry */
    }
    mrp_dbus_msg_exit_container(msg); /* array entry */

    return TRUE;
}

static void property_reply_handler(mrp_dbus_t *dbus, mrp_dbus_msg_t *msg,
        void *data)
{
    dbus_property_watch_t *w = (dbus_property_watch_t *) data;

    MRP_UNUSED(dbus);

    mrp_log_info("AMB: received property method reply, going for %s handling",
            w->tdata ? "basic" : "lua");

    if (w->tdata) {
        basic_property_handler(msg, w);
    }
    else {
        lua_property_handler(msg, w);
    }
}


static void find_property_reply_handler(mrp_dbus_t *dbus, mrp_dbus_msg_t *msg,
        void *data)
{
    dbus_property_watch_t *w = (dbus_property_watch_t *) data;
    char *obj = NULL;

    MRP_UNUSED(dbus);

    if (mrp_dbus_msg_type(msg) == MRP_DBUS_MESSAGE_TYPE_ERROR) {
        mrp_log_error("AMB: Error when trying to find an AMB object path");
        goto error;
    }

    /* Array of object paths */

    if (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_ARRAY) {
        mrp_log_error("AMB: FindObject response doesn't contain an array");
        goto error;
    }

    mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_ARRAY, NULL);

    /* We take the first one for now. */

    /* TODO: open this for configuration? What does it mean if there are
       multiple VehicleSpeed property objects, for example? */

    if (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_OBJECT_PATH) {
        mrp_log_error("AMB: no objects in the object path array");
        goto error;
    }

    if(!mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_OBJECT_PATH, &obj)) {
        mrp_log_error("AMB: Error fetching the object path from the message");
        goto error;
    }

    mrp_dbus_msg_exit_container(msg); /* array */

    mrp_free((char *) w->lua_prop->dbus_data.obj);
    w->lua_prop->dbus_data.obj = mrp_strdup(obj);

    mrp_debug("AMB: path for property %s: %s", w->lua_prop->dbus_data.name,
            w->lua_prop->dbus_data.obj);

    subscribe_property(w->ctx, w);

error:
    return;
}


static int find_property_object(data_t *ctx, dbus_property_watch_t *w,
        const char *prop)
{
    if (!ctx || !w || !prop)
        return -1;

    mrp_log_info("AMB: finding object path of property '%s'", prop);

    mrp_dbus_call(ctx->dbus,
            ctx->amb_addr, "/",
            "org.automotive.Manager",
            "FindObject", 3000, find_property_reply_handler, w,
            MRP_DBUS_TYPE_STRING, prop, MRP_DBUS_TYPE_INVALID);

    return 0;
}

static void free_dbus_property_object(dbus_property_object_t *o)
{
    mrp_htbl_destroy(o->dbus_properties, FALSE);
    mrp_free(o->path);
    mrp_free(o);
}

static void htbl_free_prop(void *key, void *object)
{
    MRP_UNUSED(key);
    MRP_UNUSED(object);
}

static int subscribe_property(data_t *ctx, dbus_property_watch_t *w)
{
    const char *obj = w->lua_prop->dbus_data.obj;
    const char *iface = w->lua_prop->dbus_data.iface;
    const char *name = w->lua_prop->dbus_data.name;
    dbus_property_object_t *o;
    int interface_len = strlen(w->lua_prop->dbus_data.iface);
    int name_len = strlen(w->lua_prop->dbus_data.name);

    mrp_log_info("AMB: subscribing to PropertiesChanged signal at '%s'", obj);

    o = (dbus_property_object_t *) mrp_htbl_lookup(ctx->dbus_property_objects,
            (void *) obj);

    if (!o) {
        mrp_htbl_config_t prop_conf;

        prop_conf.comp = mrp_string_comp;
        prop_conf.hash = mrp_string_hash;
        prop_conf.free = htbl_free_prop;
        prop_conf.nbucket = 0;
        prop_conf.nentry = 10;

        o = (dbus_property_object_t *) mrp_allocz(sizeof(dbus_property_object_t));
        o->path = strdup(obj);
        if (!o->path)
            return -1;
        o->dbus_properties = mrp_htbl_create(&prop_conf);
        if (!o->dbus_properties) {
            mrp_free(o->path);
            return -1;
        }

        mrp_htbl_insert(ctx->dbus_property_objects, o->path, o);
    }

    /* generate the key and add the watch to the object */

    w->key = mrp_allocz(interface_len + 1 + name_len + 1);

    if (!w->key) {
        free_dbus_property_object(o);
        return -1;
    }

    strncpy(w->key, iface, interface_len);
    *(w->key+interface_len) = '-';
    strncpy(w->key+interface_len+1, name, name_len);

    mrp_debug("AMB: inserting property watch to %p with key '%s'", o, w->key);

    mrp_htbl_insert(o->dbus_properties, w->key, w);
    w->o = o;

    mrp_dbus_subscribe_signal(ctx->dbus, property_signal_handler, o, NULL,
            obj, "org.freedesktop.DBus.Properties", "PropertiesChanged", NULL);

    /* Ok, now we are listening to property changes. Let's get the initial
     * value. */

    mrp_dbus_call(ctx->dbus,
            ctx->amb_addr, obj,
            "org.freedesktop.DBus.Properties",
            "Get", 3000, property_reply_handler, w,
            MRP_DBUS_TYPE_STRING, iface,
            MRP_DBUS_TYPE_STRING, name,
            MRP_DBUS_TYPE_INVALID);

    return 0;
}


static void print_basic_property(dbus_basic_property_t *prop)
{
    switch (prop->type) {
        case MRP_DBUS_TYPE_INT32:
        case MRP_DBUS_TYPE_INT16:
            mrp_debug("AMB: Property %s : %i", prop->name, prop->value.i);
            break;
        case MRP_DBUS_TYPE_UINT32:
        case MRP_DBUS_TYPE_UINT16:
        case MRP_DBUS_TYPE_BOOLEAN:
        case MRP_DBUS_TYPE_BYTE:
            mrp_debug("AMB: Property %s : %u", prop->name, prop->value.u);
            break;
        case MRP_DBUS_TYPE_DOUBLE:
            mrp_debug("AMB: Property %s : %f", prop->name, prop->value.f);
            break;
        case MRP_DBUS_TYPE_STRING:
            mrp_debug("AMB: Property %s : %s", prop->name, prop->value.s);
            break;
        default:
            mrp_log_error("AMB: Unknown value in property");
    }
}

static void basic_property_updated(dbus_basic_property_t *prop, void *userdata)
{
    char buf[512];
    int buflen;
    mql_result_t *r;
    dbus_property_watch_t *w = (dbus_property_watch_t *) userdata;
    basic_table_data_t *tdata = w->tdata;
    mqi_handle_t tx;

    mrp_debug("AMB: basic_property_updated");

    print_basic_property(prop);

    tx = mqi_begin_transaction();

    if (!prop->initialized) {

        switch (tdata->type) {
            case mqi_string:
                buflen = snprintf(buf, 512, "INSERT INTO %s VALUES (1, '%s', %s)",
                    w->lua_prop->basic_table_name, prop->name, prop->value.s);
                break;
            case mqi_integer:
                buflen = snprintf(buf, 512, "INSERT INTO %s VALUES (1, '%s', %d)",
                    w->lua_prop->basic_table_name, prop->name, (int) prop->value.i);
                break;
            case mqi_unsignd:
                buflen = snprintf(buf, 512, "INSERT INTO %s VALUES (1, '%s', %u)",
                    w->lua_prop->basic_table_name, prop->name, prop->value.u);
                break;
            case mqi_floating:
                buflen = snprintf(buf, 512, "INSERT INTO %s VALUES (1, '%s', %f)",
                    w->lua_prop->basic_table_name, prop->name, prop->value.f);
                break;
            default:
                goto end;
        }

        if (buflen <= 0 || buflen == 512) {
            goto end;
        }

        r = mql_exec_string(mql_result_string, buf);

        prop->initialized = TRUE;
    }
    else {
        int ret;

        switch (tdata->type) {
            case mqi_string:
                ret = mql_bind_value(tdata->update_operation, 1, tdata->type,
                        prop->value.s);
                break;
            case mqi_integer:
                ret = mql_bind_value(tdata->update_operation, 1, tdata->type,
                        prop->value.i);
                break;
            case mqi_unsignd:
                ret = mql_bind_value(tdata->update_operation, 1, tdata->type,
                        prop->value.u);
                break;
            case mqi_floating:
                ret = mql_bind_value(tdata->update_operation, 1, tdata->type,
                        prop->value.f);
                break;
            default:
                goto end;
        }

        if (ret < 0) {
            mrp_log_error("AMB: failed to bind value to update operation");
            goto end;
        }

        r = mql_exec_statement(mql_result_string, tdata->update_operation);
    }

    mrp_debug("amb: %s", mql_result_is_success(r) ? "updated database" :
              mql_result_error_get_message(r));

    mql_result_free(r);

end:
    mqi_commit_transaction(tx);
}

static void delete_basic_table_data(basic_table_data_t *tdata)
{
    if (!tdata)
        return;

    if (tdata->update_operation)
        mql_statement_free(tdata->update_operation);

    if (tdata->table)
        mqi_drop_table(tdata->table);

    mrp_free(tdata);
}

static int create_amb_state_table()
{
    mqi_handle_t table;
    mqi_column_def_t defs[3];
    char buf[512];
    int buflen;
    mql_result_t *r;
    mqi_handle_t tx;

    defs[0].name = "id";
    defs[0].type = mqi_unsignd;
    defs[0].flags = 0;

    defs[1].name = "state";
    defs[1].type = mqi_integer;
    defs[1].flags = 0;

    memset(&defs[2], 0, sizeof(defs[2]));

    table = MQI_CREATE_TABLE(AMB_STATE_TABLE_NAME, MQI_TEMPORARY, defs, NULL);

    if (!table)
        return -1;

    /* initial value: unknown (-1) */

    buflen = snprintf(buf, 512, "INSERT INTO %s VALUES (0, %d)",
            AMB_STATE_TABLE_NAME, -1);

    if (buflen <= 0 || buflen == 512) {
        return -1;
    }

    tx = mqi_begin_transaction();

    mrp_log_info("AMB: '%s'", buf);

    r = mql_exec_string(mql_result_string, buf);
    mql_result_free(r);

    mqi_commit_transaction(tx);

    return 0;
}

static int update_amb_state_table(data_t *ctx, int state)
{
    char buf[512];
    int buflen;
    mql_result_t *r;
    mqi_handle_t tx;

    MRP_UNUSED(ctx);

    buflen = snprintf(buf, 512, "UPDATE %s SET state = +%d where id = 0",
            AMB_STATE_TABLE_NAME, state);

    if (buflen <= 0 || buflen == 512) {
        return -1;
    }

    tx = mqi_begin_transaction();

    mrp_log_info("AMB: '%s'", buf);

    r = mql_exec_string(mql_result_string, buf);
    mql_result_free(r);

    mqi_commit_transaction(tx);

    return 0;
}


static basic_table_data_t *create_basic_property_table(const char *table_name,
        const char *member, int type)
{
    char buf[512];
    char *update_format;
    /* char *insert_format; */
    basic_table_data_t *tdata = NULL;
    int ret;

    if (strlen(member) > 64)
        goto error;

    tdata = (basic_table_data_t *) mrp_allocz(sizeof(basic_table_data_t));

    if (!tdata)
        goto error;

    switch (type) {
        case MRP_DBUS_TYPE_INT32:
        case MRP_DBUS_TYPE_INT16:
            tdata->type = mqi_integer;
            update_format = "%d";
            /* insert_format = "%d"; */
            break;
        case MRP_DBUS_TYPE_UINT32:
        case MRP_DBUS_TYPE_UINT16:
        case MRP_DBUS_TYPE_BOOLEAN:
        case MRP_DBUS_TYPE_BYTE:
            tdata->type = mqi_unsignd;
            update_format = "%u";
            /* insert_format = "%u"; */
            break;
        case MRP_DBUS_TYPE_DOUBLE:
            tdata->type = mqi_floating;
            update_format = "%f";
            /* insert_format = "%f"; */
            break;
        case MRP_DBUS_TYPE_STRING:
            tdata->type = mqi_varchar;
            update_format = "%s";
            /* insert_format = "'%s'"; */
            break;
        default:
            mrp_log_error("unknown type %d", type);
            goto error;
    }

    tdata->defs[0].name = "id";
    tdata->defs[0].type = mqi_unsignd;
    tdata->defs[0].length = 0;
    tdata->defs[0].flags = 0;

    tdata->defs[1].name = "key";
    tdata->defs[1].type = mqi_varchar;
    tdata->defs[1].length = 64;
    tdata->defs[1].flags = 0;

    tdata->defs[2].name = "value";
    tdata->defs[2].type = tdata->type;
    tdata->defs[2].length = (tdata->type == mqi_varchar) ? 128 : 0;
    tdata->defs[2].flags = 0;

    memset(&tdata->defs[3], 0, sizeof(tdata->defs[3]));

    tdata->table = MQI_CREATE_TABLE((char *) table_name, MQI_TEMPORARY,
            tdata->defs, NULL);

    if (!tdata->table) {
        mrp_log_error("AMB: creating table '%s' failed", table_name);
        goto error;
    }

    ret = snprintf(buf, 512, "UPDATE %s SET value = %s where id = 1",
            table_name, update_format);

    if (ret <= 0 || ret == 512) {
        goto error;
    }

    tdata->update_operation = mql_precompile(buf);

    if (!tdata->update_operation) {
        mrp_log_error("AMB: buggy buf: '%s'", buf);
        goto error;
    }

    mrp_log_info("AMB: compiled update statement '%s'", buf);

    return tdata;

error:
    mrp_log_error("AMB: failed to create table %s", table_name);
    delete_basic_table_data(tdata);
    return NULL;
}

static int load_config(lua_State *L, const char *path)
{
    if (!luaL_loadfile(L, path) && !lua_pcall(L, 0, 0, 0))
        return TRUE;
    else {
        mrp_log_error("AMB: failed to load config file %s.", path);
        mrp_log_error("%s", lua_tostring(L, -1));
        lua_settop(L, 0);

        return FALSE;
    }
}

static int unsubscribe_signal_cb(void *key, void *object, void *user_data)
{
    dbus_property_object_t *o = (dbus_property_object_t *) object;
    data_t *ctx = (data_t *) user_data;

    MRP_UNUSED(key);

    mrp_dbus_unsubscribe_signal(ctx->dbus, property_signal_handler, o,
            NULL, o->path,
            "org.freedesktop.DBus.Properties",
            "PropertiesChanged", NULL);
    return MRP_HTBL_ITER_MORE;
}

/* functions for handling updating the AMB properties */

static int update_amb_property(char *name, enum amb_type type, void *value,
        data_t *ctx)
{
    int ret = -1;
    mrp_msg_t *msg = mrp_msg_create(
            MRP_MSG_TAG_STRING(1, name),
            MRP_MSG_FIELD_END);

    if (!msg)
        goto end;

    if (!ctx->t)
        goto end;

    switch(type) {
        case amb_string:
            mrp_msg_append(msg, 2, MRP_MSG_FIELD_STRING, value);
            break;
        case amb_bool:
            mrp_msg_append(msg, 2, MRP_MSG_FIELD_BOOL, *(bool *)value);
            break;
        case amb_int32:
            mrp_msg_append(msg, 2, MRP_MSG_FIELD_INT32, *(int32_t *)value);
            break;
        case amb_int16:
            mrp_msg_append(msg, 2, MRP_MSG_FIELD_INT16, *(int16_t *)value);
            break;
        case amb_uint32:
            mrp_msg_append(msg, 2, MRP_MSG_FIELD_UINT32, *(uint32_t *)value);
            break;
        case amb_uint16:
            mrp_msg_append(msg, 2, MRP_MSG_FIELD_UINT16, *(uint16_t *)value);
            break;
        case amb_byte:
            mrp_msg_append(msg, 2, MRP_MSG_FIELD_UINT8, *(uint8_t *)value);
            break;
        case amb_double:
            mrp_msg_append(msg, 2, MRP_MSG_FIELD_DOUBLE, *(double *)value);
            break;
    }

    if (!mrp_transport_send(ctx->t, msg)) {
        mrp_log_error("AMB: failed to send message ambd");
        goto end;
    }

    ret = 0;

end:
    if (msg)
        mrp_msg_unref(msg);

    return ret;
}


static bool initiate_func(lua_State *L, void *data,
                    const char *signature, mrp_funcbridge_value_t *args,
                    char  *ret_type, mrp_funcbridge_value_t *ret_val)
{
    MRP_UNUSED(L);
    MRP_UNUSED(args);
    MRP_UNUSED(data);

    if (!signature || signature[0] != 'o') {
        return false;
    }

    *ret_type = MRP_FUNCBRIDGE_BOOLEAN;
    ret_val->boolean = true;

    return TRUE;
}


static bool update_func(lua_State *L, void *data,
                    const char *signature, mrp_funcbridge_value_t *args,
                    char  *ret_type, mrp_funcbridge_value_t *ret_val)
{
    mrp_lua_sink_t *sink;
    const char *type, *property;

    const char *s_val;
    int32_t i_val;
    uint32_t u_val;
    double d_val;

    int ret = -1;
    char *error = "unknown error";

    data_t *ctx = (data_t *) data;
    int property_index;

    MRP_UNUSED(L);

    if (!ctx->t) {
        error = "ambd is not connected to Murphy";
        goto error;
    }

    if (!signature || signature[0] != 'o') {
        mrp_log_error("AMB: invalid signature '%s'",
                signature ? signature : "NULL");
        goto error;
    }

    sink = (mrp_lua_sink_t *) args[0].pointer;

    property = mrp_lua_sink_get_property(sink);

    if (!property || strlen(property) == 0) {
        error = "invalid property";
        goto error;
    }

    property_index = mrp_lua_sink_get_input_index(sink, property);

    if (property_index == -1) {
        error = "invalid property index";
        goto error;
    }

    if (mrp_lua_sink_get_row_count(sink, property_index) == 0) {
        mrp_log_warning("AMB: no value to report -- no rows in property");
        goto end;
    }

    /* ok, for now we only support updates of basic values */

    type = mrp_lua_sink_get_type(sink);

    if (!type || strlen(type) != 1) {
        error = "invalid type";
        goto error;
    }

    switch (type[0]) {
        case amb_double:
            d_val = mrp_lua_sink_get_floating(sink,property_index,0,0);
            mrp_log_info("value for '%s' : %f", property, d_val);
            ret = update_amb_property((char *) property,
                    (enum amb_type) type[0], &d_val, ctx);
            break;
        case amb_int16:
        case amb_int32:
            i_val = mrp_lua_sink_get_integer(sink,property_index,0,0);
            mrp_log_info("value for '%s' : %d", property, i_val);
            ret = update_amb_property((char *) property,
                    (enum amb_type) type[0], &i_val, ctx);
            break;
        case amb_bool:
        case amb_byte:
        case amb_uint16:
        case amb_uint32:
            u_val = mrp_lua_sink_get_unsigned(sink,property_index,0,0);
            mrp_log_info("value for '%s' : %u", property, u_val);
            ret = update_amb_property((char *) property,
                    (enum amb_type) type[0], &u_val, ctx);
            break;
        case amb_string:
            s_val = mrp_lua_sink_get_string(sink,property_index,0,0,NULL,0);
            mrp_log_info("value for '%s' : %s", property, s_val);
            ret = update_amb_property((char *) property,
                    (enum amb_type) type[0], (void *) s_val, ctx);
            break;
    }

    if (ret < 0) {
        error = "error updating property";
        goto error;
    }

end:
    *ret_type = MRP_FUNCBRIDGE_BOOLEAN;
    ret_val->boolean = true;

    return TRUE;

error:
    mrp_log_error("AMB: error sending property change to ambd: %s!", error);

    *ret_type = MRP_FUNCBRIDGE_BOOLEAN;
    ret_val->boolean = false;

    /* FIXME: this shouldn't be needed, but without this the element handler
       crashes */
    ret_val->string = mrp_strdup(error);

    return TRUE;
}


static void recvdatafrom_evt(mrp_transport_t *t, void *data, uint16_t tag,
                     mrp_sockaddr_t *addr, socklen_t addrlen, void *user_data)
{
    /* At the moment we are not receiving anything from AMB through this
     * transport, however that might change */

    MRP_UNUSED(t);
    MRP_UNUSED(data);
    MRP_UNUSED(tag);
    MRP_UNUSED(addr);
    MRP_UNUSED(addrlen);
    MRP_UNUSED(user_data);
}


static void recvdata_evt(mrp_transport_t *t, void *data, uint16_t tag, void *user_data)
{
    recvdatafrom_evt(t, data, tag, NULL, 0, user_data);
}


static void closed_evt(mrp_transport_t *t, int error, void *user_data)
{
    data_t *ctx = (data_t *) user_data;

    MRP_UNUSED(error);

    mrp_transport_destroy(t);
    ctx->t = NULL;
    update_amb_state_table(ctx, 0);

    /* open the listening socket again */

    if (!ctx->lt) {
        create_transport(t->ml, ctx);
    }
}

static void connection_evt(mrp_transport_t *lt, void *user_data)
{
    data_t *ctx = (data_t *) user_data;

    mrp_log_info("AMB connection!");

    if (ctx->t) {
        mrp_log_error("AMB: already connected");
    }
    else {
        ctx->t = mrp_transport_accept(lt, ctx, 0);
        update_amb_state_table(ctx, 1);

        /* amb murphy plugin is now connected to us */
    }

    /* close the listening socket, since we only have one client */

    mrp_transport_destroy(lt);
    ctx->lt = NULL;
}

static int create_transport(mrp_mainloop_t *ml, data_t *ctx)
{
    socklen_t alen;
    mrp_sockaddr_t addr;
    int flags = MRP_TRANSPORT_REUSEADDR | MRP_TRANSPORT_MODE_MSG;
    const char *atype;
    struct stat statbuf;

    static mrp_transport_evt_t evt; /* static members are initialized to zero */

    evt.closed = closed_evt;
    evt.connection = connection_evt;
    evt.recvdata = recvdata_evt;
    evt.recvdatafrom = recvdatafrom_evt;

    alen = mrp_transport_resolve(NULL, ctx->tport_addr, &addr, sizeof(addr),
            &atype);
    if (alen <= 0) {
        mrp_log_error("AMB: failed to resolve address");
        goto error;
    }

    /* remove the old socket if present */

    if (strcmp(atype, "unxs") == 0) {
        char *path = addr.unx.sun_path;
        if (path[0] == '/') {
            /* if local socket and file exists, remove it */
            if (stat(path, &statbuf) == 0) {
                if (S_ISSOCK(statbuf.st_mode)) {
                    if (unlink(path) < 0) {
                        mrp_log_error("error removing the socket");
                        goto error;
                    }
                }
                else {
                    mrp_log_error("AMB: a file where the socket should be");
                    goto error;
                }
            }
        }
    }


    ctx->lt = mrp_transport_create(ml, atype, &evt, ctx, flags);
    if (ctx->lt == NULL) {
        mrp_log_error("AMB: failed to create transport");
        goto error;
    }

    if (!mrp_transport_bind(ctx->lt, &addr, alen)) {
        mrp_log_error("AMB: failed to bind transport to address");
        goto error;
    }

    if (!mrp_transport_listen(ctx->lt, 1)) {
        mrp_log_error("AMB: failed to listen on transport");
        goto error;
    }

    return 0;

error:
    if (ctx->lt)
        mrp_transport_destroy(ctx->lt);

    return -1;
}


static void htbl_free_obj(void *key, void *object)
{
    dbus_property_object_t *o = (dbus_property_object_t *) object;

    MRP_UNUSED(key);

    free_dbus_property_object(o);
}

void amb_state_cb(mrp_dbus_t *dbus, const char *name, int up, const char *owner,
        void *user_data)
{
    data_t *ctx = (data_t *) user_data;
    mrp_list_hook_t *p, *n;

    MRP_UNUSED(dbus);
    MRP_UNUSED(name);
    MRP_UNUSED(owner);

    mrp_log_info("AMB: ambd D-Bus interface was set to: %d", up);

    if (up) {
        mrp_log_info("subscribing properties");
        /* amb D-Bus interface was brought up, subscribe properties */
        mrp_list_foreach(&ctx->lua_properties, p, n) {
            dbus_property_watch_t *w =
                    mrp_list_entry(p, dbus_property_watch_t, hook);

            if (w->lua_prop->dbus_data.undefined_object_path)
                find_property_object(ctx, w, w->lua_prop->dbus_data.objectname);
            else
                subscribe_property(ctx, w);
        }
    }
    else {
        mrp_log_info("unsubscribing properties");
        /* unsubscribe properties? The paths should be deterministic. */
        mrp_htbl_foreach(ctx->dbus_property_objects, unsubscribe_signal_cb,
                ctx);
    }
}

/* plugin init and deinit */

static int amb_init(mrp_plugin_t *plugin)
{
    data_t *ctx;
    mrp_plugin_arg_t *args = plugin->args;
    mrp_htbl_config_t obj_conf;

    ctx = (data_t *) mrp_allocz(sizeof(data_t));

    if (!ctx)
        return FALSE;

    mrp_list_init(&ctx->lua_properties);

    plugin->data = ctx;

    ctx->ml = plugin->ctx->ml;

    ctx->amb_addr = args[ARG_AMB_DBUS_ADDRESS].str;
    ctx->config_file = args[ARG_AMB_CONFIG_FILE].str;
    ctx->amb_id = args[ARG_AMB_ID].str;
    ctx->tport_addr = args[ARG_AMB_TPORT_ADDRESS].str;

    mrp_log_info("AMB: D-Bus address: %s", ctx->amb_addr);
    mrp_log_info("AMB: config file: %s", ctx->config_file);
    mrp_log_info("AMB: transport address: %s", ctx->tport_addr);

    ctx->dbus = mrp_dbus_connect(plugin->ctx->ml, args[ARG_AMB_DBUS_BUS].str,
            NULL);

    if (!ctx->dbus)
        goto error;

    /* initialize transport towards ambd */

    if (create_transport(plugin->ctx->ml, ctx) < 0)
        goto error;

    /* create hash table */

    obj_conf.comp = mrp_string_comp;
    obj_conf.hash = mrp_string_hash;
    obj_conf.free = htbl_free_obj;
    obj_conf.nbucket = 0;
    obj_conf.nentry = 10;

    ctx->dbus_property_objects = mrp_htbl_create(&obj_conf);

    if (!ctx->dbus_property_objects)
        goto error;

    /* initialize lua support */

    global_ctx = ctx;

    ctx->L = mrp_lua_get_lua_state();

    if (!ctx->L)
        goto error;

    /* functions to handle the direct property updates */

    mrp_funcbridge_create_cfunc(ctx->L, "amb_initiate", "o",
                                initiate_func, (void *)ctx);
    mrp_funcbridge_create_cfunc(ctx->L, "amb_update", "o",
                                update_func, (void *)ctx);

    /* custom class for configuration */

    mrp_lua_create_object_class(ctx->L, MRP_LUA_CLASS(amb, property));

    /* TODO: create here a "manager" lua object and put that to the global
     * lua table? This one then has a pointer to the C context. */

    /* 1. read the configuration file. The configuration must tell
            - target object (/org/automotive/runningstatus/vehicleSpeed)
            - target interface (org.automotive.vehicleSpeed)
            - target member (VehicleSpeed)
            - target type (int32_t)
            - destination table
     */

    if (!load_config(ctx->L, ctx->config_file))
        goto error;

    mrp_process_set_state("murphy-amb", MRP_PROCESS_STATE_READY);

    /* keep track if amb is connected to Murphy*/
    create_amb_state_table();

    /* start following the amb D-Bus name */

    mrp_dbus_follow_name(ctx->dbus, ctx->amb_addr, amb_state_cb, ctx);

    return TRUE;

error:
    {
        mrp_list_hook_t *p, *n;

        mrp_list_foreach(&ctx->lua_properties, p, n) {
            dbus_property_watch_t *w =
                    mrp_list_entry(p, dbus_property_watch_t, hook);

            destroy_prop(ctx, w);
        }
    }

    if (ctx->dbus) {
        mrp_dbus_unref(ctx->dbus);
        ctx->dbus = NULL;
    }

    if (ctx->t) {
        mrp_transport_destroy(ctx->t);
        ctx->t = NULL;
    }

    if (ctx->dbus_property_objects) {
        mrp_htbl_destroy(ctx->dbus_property_objects, FALSE);
    }

    mrp_process_remove_watch(ctx->amb_id);

    mrp_free(ctx);

    return FALSE;
}


static void amb_exit(mrp_plugin_t *plugin)
{
    data_t *ctx = (data_t *) plugin->data;
    mrp_list_hook_t *p, *n;

    mrp_process_remove_watch(ctx->amb_id);

    mrp_process_set_state("murphy-amb", MRP_PROCESS_STATE_NOT_READY);

    /* for all subscribed properties, unsubscribe and free memory */

    mrp_list_foreach(&ctx->lua_properties, p, n) {
        dbus_property_watch_t *w =
                mrp_list_entry(p, dbus_property_watch_t, hook);

        destroy_prop(ctx, w);
    }

    if (ctx->dbus_property_objects) {
        mrp_htbl_destroy(ctx->dbus_property_objects, FALSE);
    }

    mrp_transport_destroy(ctx->t);
    ctx->t = NULL;

    global_ctx = NULL;

    mrp_free(ctx);
}

#define AMB_DESCRIPTION "A plugin for Automotive Message Broker D-Bus API."
#define AMB_HELP        "Access Automotive Message Broker."
#define AMB_VERSION     MRP_VERSION_INT(0, 0, 2)
#define AMB_AUTHORS     "Ismo Puustinen <ismo.puustinen@intel.com>"

static mrp_plugin_arg_t args[] = {
    MRP_PLUGIN_ARGIDX(ARG_AMB_DBUS_ADDRESS, STRING, "amb_address",
            "org.automotive.message.broker"),
    MRP_PLUGIN_ARGIDX(ARG_AMB_DBUS_BUS, STRING, "dbus_bus", "system"),
    MRP_PLUGIN_ARGIDX(ARG_AMB_CONFIG_FILE, STRING, "config_file",
            "/etc/murphy/plugins/amb/config.lua"),
    MRP_PLUGIN_ARGIDX(ARG_AMB_ID, STRING, "amb_id", "ambd"),
    MRP_PLUGIN_ARGIDX(ARG_AMB_TPORT_ADDRESS, STRING, "transport_address",
            "unxs:/tmp/murphy/amb"),
};

MURPHY_REGISTER_PLUGIN("amb",
                       AMB_VERSION, AMB_DESCRIPTION,
                       AMB_AUTHORS, AMB_HELP,
                       MRP_SINGLETON, amb_init, amb_exit,
                       args, MRP_ARRAY_SIZE(args),
                       NULL, 0, NULL, 0, NULL);
