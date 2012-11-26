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

#include <murphy/common.h>
#include <murphy/core.h>
#include <murphy/common/dbus.h>

#include <murphy-db/mdb.h>
#include <murphy-db/mqi.h>
#include <murphy-db/mql.h>

#include <murphy/core/lua-bindings/murphy.h>
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/core/lua-decision/mdb.h>

enum {
    ARG_AMB_DBUS_ADDRESS,
    ARG_AMB_CONFIG_FILE
};

#define AMB_NAME                "name"
#define AMB_HANDLER             "handler"
#define AMB_DBUS_DATA           "dbus_data"
#define AMB_OBJECT              "obj"
#define AMB_INTERFACE           "interface"
#define AMB_MEMBER              "property"
#define AMB_SIGNATURE           "signature"
#define AMB_BASIC_TABLE_NAME    "basic_table_name"
#define AMB_OUTPUTS             "outputs"



/*
signal sender=:1.117 -> dest=(null destination) serial=961
path=/org/automotive/runningstatus/vehicleSpeed;
interface=org.automotive.vehicleSpeed;
member=VehicleSpeed
   variant       int32 0


dbus-send --system --print-reply --dest=org.automotive.message.broker \
        /org/automotive/runningstatus/vehicleSpeed \
        org.freedesktop.DBus.Properties.Get \
        string:'org.automotive.vehicleSpeed' string:'VehicleSpeed'

method return sender=:1.69 -> dest=:1.91 reply_serial=2
   variant       int32 0
*/

typedef struct {
    char *name;
    int type;
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
        const char *obj;
        const char *iface;
        const char *name;
        const char *sig;
    } dbus_data;
    const char *name;
    const char *basic_table_name;
    int handler_ref;
    int outputs_ref;
} lua_amb_property_t;

typedef struct {
    mrp_dbus_t *dbus;
    const char *amb_addr;
    const char *config_file;
    lua_State *L;
    mrp_list_hook_t lua_properties;
} data_t;

typedef struct {
    mqi_column_def_t defs[4];

    mql_statement_t *update_operation;
    mqi_data_type_t type;
    mqi_handle_t table;
} basic_table_data_t;

typedef struct {
    dbus_basic_property_t prop;

    property_updated_cb_t cb;
    void *user_data;

    lua_amb_property_t *lua_prop;

    /* for basic tables that we manage ourselves */
    basic_table_data_t *tdata;

    mrp_list_hook_t hook;
    data_t *ctx;
} dbus_property_watch_t;

static data_t *global_ctx = NULL;

static basic_table_data_t *create_basic_property_table(const char *table_name,
        const char *member, int type);

static int subscribe_property(data_t *ctx, dbus_property_watch_t *w);

static void basic_property_updated(dbus_basic_property_t *prop, void *userdata);


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

    mrp_log_info("> lua_amb_destroy");

    MRP_UNUSED(prop);

    MRP_LUA_LEAVE_NOARG;
}


static void destroy_prop(data_t *ctx, dbus_property_watch_t *w)
{
    /* TODO */

    MRP_UNUSED(ctx);
    MRP_UNUSED(w);
}


static int amb_constructor(lua_State *L)
{
    lua_amb_property_t *prop;
    size_t field_name_len;
    const char *field_name;
    data_t *ctx = global_ctx;
    dbus_property_watch_t *w;

    MRP_LUA_ENTER;

    mrp_log_info("> amb_constructor, stack size: %d", lua_gettop(L));

    prop = mrp_lua_create_object(L, PROPERTY_CLASS, NULL);

    prop->handler_ref = LUA_NOREF;
    prop->outputs_ref = LUA_NOREF;

    MRP_LUA_FOREACH_FIELD(L, 2, field_name, field_name_len) {
        char buf[field_name_len+1];

        strncpy(buf, field_name, field_name_len);
        buf[field_name_len] = '\0';

        mrp_log_info("field name: %s", buf);

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

                if (!key || !value)
                    goto error;

                if (strcmp(key, "signature") == 0) {
                    prop->dbus_data.sig = mrp_strdup(value);
                }
                else if (strcmp(key, "property") == 0) {
                    prop->dbus_data.name = mrp_strdup(value);
                }
                else if (strcmp(key, "obj") == 0) {
                    prop->dbus_data.obj = mrp_strdup(value);
                }
                else if (strcmp(key, "interface") == 0) {
                    prop->dbus_data.iface = mrp_strdup(value);
                }
                else {
                    goto error;
                }

                lua_pop(L, 1);
            }

            /* check that we have all necessary data */
            if (prop->dbus_data.sig == NULL ||
                prop->dbus_data.iface == NULL ||
                prop->dbus_data.obj == NULL ||
                prop->dbus_data.name == NULL) {
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

    if (!prop->name)
        goto error;

    if (prop->handler_ref == LUA_NOREF && !prop->basic_table_name)
        goto error;

    w = mrp_allocz(sizeof(dbus_property_watch_t));

    w->ctx = ctx;
    w->lua_prop = prop;
    w->prop.initialized = FALSE;
    w->prop.name = mrp_strdup(w->lua_prop->dbus_data.name);
    w->prop.type = DBUS_TYPE_INVALID;

    if (prop->handler_ref == LUA_NOREF) {
        basic_table_data_t *tdata;

        w->prop.type = w->lua_prop->dbus_data.sig[0]; /* FIXME */

        tdata = create_basic_property_table(prop->basic_table_name,
            prop->dbus_data.name, w->prop.type);

        if (!tdata) {
            goto error;
        }

        w->tdata = tdata;

        w->cb = basic_property_updated;
        w->user_data = w;

        /* add_table_data(tdata, ctx); */
        if (subscribe_property(ctx, w)) {
            mrp_log_error("Failed to subscribe to basic property");
            goto error;
        }
    }
    else {
        /* we now have the callback function reference */

        /* TODO: refactor to decouple updating the property (calling the
         * lua handler) from parsing the D-Bus message. Is this possible? */
        if (subscribe_property(ctx, w)) {
            mrp_log_error("Failed to subscribe to basic property");
            goto error;
        }
    }


    mrp_list_init(&w->hook);

    mrp_list_append(&ctx->lua_properties, &w->hook);

    /* TODO: need some mapping? or custom property_watch? */

    /* TODO: put the object to a global table or not? maybe better to just
     * unload them when the plugin is unloaded. */

    mrp_lua_push_object(L, prop);

    MRP_LUA_LEAVE(1);

error:
    /* TODO: delete the allocated data */
    destroy_prop(global_ctx, w);

    mrp_log_error("< amb_constructor ERROR");
    MRP_LUA_LEAVE(0);
}

static int amb_getfield(lua_State *L)
{
    lua_amb_property_t *prop = mrp_lua_check_object(L, PROPERTY_CLASS, 1);
    size_t field_name_len;
    const char *field_name = lua_tolstring(L, 2, &field_name_len);

    MRP_LUA_ENTER;

    if (!prop)
        goto error;

    mrp_log_info("> amb_getfield");

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
        lua_pushstring(L, prop->dbus_data.obj);
        lua_settable(L, -3);

        lua_pushstring(L, AMB_INTERFACE);
        lua_pushstring(L, prop->dbus_data.iface);
        lua_settable(L, -3);

        lua_pushstring(L, AMB_MEMBER);
        lua_pushstring(L, prop->dbus_data.name);
        lua_settable(L, -3);

        lua_pushstring(L, "signature");
        lua_pushstring(L, prop->dbus_data.sig);
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

    mrp_log_info("> amb_setfield");

    MRP_LUA_LEAVE(0);
}

#if 0
bool really_create_basic_handler(lua_State *L, void *data,
                    const char *signature, mrp_funcbridge_value_t *args,
                    char  *ret_type, mrp_funcbridge_value_t *ret_val)
{
    mrp_log_info("> really_create_basic_handler");



    return true;
}
#endif

/* lua config end */

static bool parse_elementary_value(lua_State *L,
        DBusMessageIter *iter, dbus_property_watch_t *w)
{
    dbus_int32_t i32_val;
    dbus_int32_t i16_val;
    dbus_uint32_t u32_val;
    dbus_uint16_t u16_val;
    uint8_t byte_val;
    dbus_bool_t b_val;
    double d_val;
    char *s_val;

    char sig;

    MRP_UNUSED(w);

    if (!iter)
        goto error;

    sig = dbus_message_iter_get_arg_type(iter);

    switch (sig) {
        case DBUS_TYPE_INT32:
            dbus_message_iter_get_basic(iter, &i32_val);
            lua_pushinteger(L, i32_val);
            break;
        case DBUS_TYPE_INT16:
            dbus_message_iter_get_basic(iter, &i16_val);
            lua_pushinteger(L, i16_val);
            break;
        case DBUS_TYPE_UINT32:
            dbus_message_iter_get_basic(iter, &u32_val);
            lua_pushinteger(L, u32_val);
            break;
        case DBUS_TYPE_UINT16:
            dbus_message_iter_get_basic(iter, &u16_val);
            lua_pushinteger(L, u16_val);
            break;
        case DBUS_TYPE_BOOLEAN:
            dbus_message_iter_get_basic(iter, &b_val);
            lua_pushboolean(L, b_val == TRUE);
            break;
        case DBUS_TYPE_BYTE:
            dbus_message_iter_get_basic(iter, &byte_val);
            lua_pushinteger(L, byte_val);
            break;
        case DBUS_TYPE_DOUBLE:
            dbus_message_iter_get_basic(iter, &d_val);
            lua_pushnumber(L, d_val);
            break;
        case DBUS_TYPE_STRING:
            dbus_message_iter_get_basic(iter, &s_val);
            lua_pushstring(L, s_val);
            break;
        default:
            mrp_log_info("> parse_elementary_value: unknown type");
            goto error;
    }

    return TRUE;

error:
    return FALSE;
}

static bool parse_value(lua_State *L, DBusMessageIter *iter,
        dbus_property_watch_t *w);

static bool parse_struct(lua_State *L,
        DBusMessageIter *iter, dbus_property_watch_t *w)
{
    int i = 1;
    DBusMessageIter new_iter;

    if (!iter)
        return FALSE;

    /* initialize the table */
    lua_newtable(L);

    dbus_message_iter_recurse(iter, &new_iter);

    while (dbus_message_iter_get_arg_type(&new_iter) != DBUS_TYPE_INVALID) {

        /* struct "index" */
        lua_pushinteger(L, i++);

        parse_value(L, &new_iter, w);
        dbus_message_iter_next(&new_iter);

        /* put the values to the table */
        lua_settable(L, -3);
    }

    return TRUE;
}


static bool parse_dict_entry(lua_State *L,
        DBusMessageIter *iter, dbus_property_watch_t *w)
{
    DBusMessageIter new_iter;

    if (!iter)
        return FALSE;

    dbus_message_iter_recurse(iter, &new_iter);

    while (dbus_message_iter_get_arg_type(&new_iter) != DBUS_TYPE_INVALID) {

        /* key must be elementary, value can be anything */

        parse_elementary_value(L, &new_iter, w);
        dbus_message_iter_next(&new_iter);

        parse_value(L, &new_iter, w);
        dbus_message_iter_next(&new_iter);

        /* put the values to the table */
        lua_settable(L, -3);
    }

    return TRUE;
}

static bool parse_array(lua_State *L,
        DBusMessageIter *iter, dbus_property_watch_t *w)
{
    DBusMessageIter new_iter;
    int element_type;

    if (!iter)
        return FALSE;

    /* the lua array */
    lua_newtable(L);

    element_type = dbus_message_iter_get_element_type(iter);

    dbus_message_iter_recurse(iter, &new_iter);

    /* the problem: if the value inside array is a dict entry, the
     * indexing of elements need to be done with dict keys instead
     * of numbers. */

    if (element_type == DBUS_TYPE_DICT_ENTRY) {
        while (dbus_message_iter_get_arg_type(&new_iter)
            != DBUS_TYPE_INVALID) {

            parse_dict_entry(L, &new_iter, w);
            dbus_message_iter_next(&new_iter);
        }
    }

    else {
        int i = 1;

        while (dbus_message_iter_get_arg_type(&new_iter)
            != DBUS_TYPE_INVALID) {

            /* array index */
            lua_pushinteger(L, i++);

            parse_value(L, &new_iter, w);
            dbus_message_iter_next(&new_iter);

            /* put the values to the table */
            lua_settable(L, -3);
        }
    }

    return TRUE;
}

static bool parse_value(lua_State *L, DBusMessageIter *iter,
        dbus_property_watch_t *w)
{
    char curr;

    if (!iter)
        return FALSE;

    curr = dbus_message_iter_get_arg_type(iter);

    switch (curr) {
        case DBUS_TYPE_BYTE:
        case DBUS_TYPE_BOOLEAN:
        case DBUS_TYPE_INT16:
        case DBUS_TYPE_INT32:
        case DBUS_TYPE_UINT16:
        case DBUS_TYPE_UINT32:
        case DBUS_TYPE_DOUBLE:
        case DBUS_TYPE_STRING:
            return parse_elementary_value(L, iter, w);
        case DBUS_TYPE_ARRAY:
            return parse_array(L, iter, w);
        case DBUS_TYPE_STRUCT:
            return parse_struct(L, iter, w);
        case DBUS_TYPE_DICT_ENTRY:
            goto error; /* these are handled from parse_array */
        case DBUS_TYPE_INVALID:
            return TRUE;
        default:
            break;
    }

error:
    mrp_log_error("failed to parse D-Bus property (sig[i] %c)", curr);
    return FALSE;
}

static void lua_property_handler(DBusMessage *msg, dbus_property_watch_t *w)
{
    DBusMessageIter msg_iter;
    DBusMessageIter variant_iter;
    const char *variant_sig;

    if (!w || !msg)
        goto error;

    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_ERROR)
        goto error;

    dbus_message_iter_init(msg, &msg_iter);

    if (dbus_message_iter_get_arg_type(&msg_iter) != DBUS_TYPE_VARIANT)
        goto error;

    dbus_message_iter_recurse(&msg_iter, &variant_iter);

    variant_sig = dbus_message_iter_get_signature(&variant_iter);

    /*
    mrp_log_info("iter sig: %s, expected: %s",
            variant_sig, w->lua_prop->dbus_data.sig);
    */

    /* check if we got what we were expecting */
    if (strcmp(variant_sig, w->lua_prop->dbus_data.sig) != 0)
        goto error;

    if (w->lua_prop->handler_ref == LUA_NOREF)
        goto error;

    /* load the function pointer to the stack */
    lua_rawgeti(w->ctx->L, LUA_REGISTRYINDEX, w->lua_prop->handler_ref);

    /* "self" parameter */
    mrp_lua_push_object(w->ctx->L, w->lua_prop);

    /* parse values to the stack */
    parse_value(w->ctx->L, &variant_iter, w);

    /* call the handler function */
    lua_pcall(w->ctx->L, 2, 0, 0);

    return;

error:
    mrp_log_error("failed to process an incoming D-Bus message");
}


static void basic_property_handler(DBusMessage *msg, dbus_property_watch_t *w)
{
    DBusMessageIter msg_iter;
    DBusMessageIter variant_iter;

    dbus_int32_t i32_val;
    dbus_int32_t i16_val;
    dbus_uint32_t u32_val;
    dbus_uint16_t u16_val;
    uint8_t byte_val;
    dbus_bool_t b_val;
    double d_val;
    char *s_val;

    if (!w || !msg)
        goto error;

    dbus_message_iter_init(msg, &msg_iter);

    if (dbus_message_iter_get_arg_type(&msg_iter) != DBUS_TYPE_VARIANT)
        goto error;

    dbus_message_iter_recurse(&msg_iter, &variant_iter);

    if (dbus_message_iter_get_arg_type(&variant_iter)
                        != w->prop.type)
        goto error;

    switch (w->prop.type) {
        case DBUS_TYPE_INT32:
            dbus_message_iter_get_basic(&variant_iter, &i32_val);
            w->prop.value.i = i32_val;
            break;
        case DBUS_TYPE_INT16:
            dbus_message_iter_get_basic(&variant_iter, &i16_val);
            w->prop.value.i = i16_val;
            break;
        case DBUS_TYPE_UINT32:
            dbus_message_iter_get_basic(&variant_iter, &u32_val);
            w->prop.value.u = u32_val;
            break;
        case DBUS_TYPE_UINT16:
            dbus_message_iter_get_basic(&variant_iter, &u16_val);
            w->prop.value.u = u16_val;
            break;
        case DBUS_TYPE_BOOLEAN:
            dbus_message_iter_get_basic(&variant_iter, &b_val);
            w->prop.value.u = b_val;
            break;
        case DBUS_TYPE_BYTE:
            dbus_message_iter_get_basic(&variant_iter, &byte_val);
            w->prop.value.u = byte_val;
            break;
        case DBUS_TYPE_DOUBLE:
            dbus_message_iter_get_basic(&variant_iter, &d_val);
            w->prop.value.f = d_val;
            break;
        case DBUS_TYPE_STRING:
            dbus_message_iter_get_basic(&variant_iter, &s_val);
            w->prop.value.s = mrp_strdup(s_val);
            break;
        default:
            goto error;
    }

    if (w->cb)
        w->cb(&w->prop, w->user_data);

    return;

error:
    mrp_log_error("amb: failed to parse property value");
    return;
}

static int property_signal_handler(mrp_dbus_t *dbus, DBusMessage *msg,
        void *data)
{
    dbus_property_watch_t *w = data;

    MRP_UNUSED(dbus);

    mrp_log_info("amb: received property signal");

    if (w->tdata) {
        basic_property_handler(msg, w);
    }
    else {
        lua_property_handler(msg, w);
    }

    return TRUE;
}

static void property_reply_handler(mrp_dbus_t *dbus, DBusMessage *msg,
        void *data)
{
    dbus_property_watch_t *w = data;

    MRP_UNUSED(dbus);

    mrp_log_info("amb: received property method reply");

    if (w->tdata) {
        basic_property_handler(msg, w);
    }
    else {
        lua_property_handler(msg, w);
    }}


static int subscribe_property(data_t *ctx, dbus_property_watch_t *w)
{
    const char *obj = w->lua_prop->dbus_data.obj;
    const char *iface = w->lua_prop->dbus_data.iface;
    const char *name = w->lua_prop->dbus_data.name;

    mrp_log_info("subscribing to signal '%s.%s' at '%s'",
            iface, name, obj);

    mrp_dbus_subscribe_signal(ctx->dbus, property_signal_handler, w, NULL,
            obj, iface, name, NULL);

    /* Ok, now we are listening to property changes. Let's get the initial
     * value. */

    mrp_dbus_call(ctx->dbus,
            ctx->amb_addr, obj,
            "org.freedesktop.DBus.Properties",
            "Get", 3000, property_reply_handler, w,
            DBUS_TYPE_STRING, &iface,
            DBUS_TYPE_STRING, &name,
            DBUS_TYPE_INVALID);

    return 0;
}


static void print_basic_property(dbus_basic_property_t *prop)
{
    switch (prop->type) {
        case DBUS_TYPE_INT32:
        case DBUS_TYPE_INT16:
            mrp_log_info("Property %s : %i", prop->name, prop->value.i);
            break;
        case DBUS_TYPE_UINT32:
        case DBUS_TYPE_UINT16:
        case DBUS_TYPE_BOOLEAN:
        case DBUS_TYPE_BYTE:
            mrp_log_info("Property %s : %u", prop->name, prop->value.u);
            break;
        case DBUS_TYPE_DOUBLE:
            mrp_log_info("Property %s : %f", prop->name, prop->value.f);
            break;
        case DBUS_TYPE_STRING:
            mrp_log_info("Property %s : %s", prop->name, prop->value.s);
            break;
        default:
            mrp_log_error("Unknown value in property");
    }
}

static void basic_property_updated(dbus_basic_property_t *prop, void *userdata)
{
    char buf[512];
    int buflen;
    mql_result_t *r;
    dbus_property_watch_t *w = userdata;
    basic_table_data_t *tdata = w->tdata;
    mqi_handle_t tx;

    mrp_log_info("> basic_property_updated");

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
                    w->lua_prop->basic_table_name, prop->name, prop->value.i);
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
            mrp_log_error("failed to bind value to update operation");
            goto end;
        }

        r = mql_exec_statement(mql_result_string, tdata->update_operation);
    }

    mrp_log_info("amb: %s", mql_result_is_success(r) ? "updated database" :
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

    tdata = mrp_allocz(sizeof(basic_table_data_t));

    if (!tdata)
        goto error;

    switch (type) {
        case DBUS_TYPE_INT32:
        case DBUS_TYPE_INT16:
            tdata->type = mqi_integer;
            update_format = "%d";
            /* insert_format = "%d"; */
            break;
        case DBUS_TYPE_UINT32:
        case DBUS_TYPE_UINT16:
        case DBUS_TYPE_BOOLEAN:
        case DBUS_TYPE_BYTE:
            tdata->type = mqi_unsignd;
            update_format = "%u";
            /* insert_format = "%u"; */
            break;
        case DBUS_TYPE_DOUBLE:
            tdata->type = mqi_floating;
            update_format = "%f";
            /* insert_format = "%f"; */
            break;
        case DBUS_TYPE_STRING:
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
        mrp_log_error("creating table '%s' failed", table_name);
        goto error;
    }

    ret = snprintf(buf, 512, "UPDATE %s SET value = %s where id = 1",
            table_name, update_format);

    if (ret <= 0 || ret == 512) {
        goto error;
    }

    tdata->update_operation = mql_precompile(buf);

    if (!tdata->update_operation) {
        mrp_log_error("buggy buf: '%s'", buf);
        goto error;
    }

    mrp_log_info("amb: compiled update statement '%s'", buf);

    return tdata;

error:
    mrp_log_error("amb: failed to create table %s", table_name);
    delete_basic_table_data(tdata);
    return NULL;
}

static int load_config(lua_State *L, const char *path)
{
    if (!luaL_loadfile(L, path) && !lua_pcall(L, 0, 0, 0))
        return TRUE;
    else {
        mrp_log_error("plugin-lua: failed to load config file %s.", path);
        mrp_log_error("%s", lua_tostring(L, -1));
        lua_settop(L, 0);

        return FALSE;
    }
}

static int amb_init(mrp_plugin_t *plugin)
{
    data_t *ctx;
    mrp_plugin_arg_t *args = plugin->args;

    ctx = mrp_allocz(sizeof(data_t));

    if (!ctx)
        goto error;

    plugin->data = ctx;

    ctx->amb_addr = args[ARG_AMB_DBUS_ADDRESS].str;
    ctx->config_file = args[ARG_AMB_CONFIG_FILE].str;

    mrp_log_info("amb dbus address: %s", ctx->amb_addr);
    mrp_log_info("amb config file: %s", ctx->config_file);

    ctx->dbus = mrp_dbus_connect(plugin->ctx->ml, "system", NULL);

    mrp_log_info("amb: 1");

    if (!ctx->dbus)
        goto error;

    mrp_log_info("amb: 2");

    /* initialize lua support */

    mrp_list_init(&ctx->lua_properties);

    global_ctx = ctx;

    ctx->L = mrp_lua_get_lua_state();

    if (!ctx->L)
        goto error;

    mrp_log_info("amb: 3");

    mrp_lua_create_object_class(ctx->L, MRP_LUA_CLASS(amb, property));

    /* TODO: create here a "manager" lua object and put that to the global
     * lua table? This one then has a pointer to the C context. */

#if 0
    mrp_funcbridge_create_cfunc(L, "create_basic_handler", "amb",
                                really_create_basic_handler, (void *)0x1234);
#endif

    /* 1. read the configuration file. The configuration must tell
            - target object (/org/automotive/runningstatus/vehicleSpeed)
            - target interface (org.automotive.vehicleSpeed)
            - target member (VehicleSpeed)
            - target type (int32_t)
            - destination table
     */

    load_config(ctx->L, ctx->config_file);

    return TRUE;

error:
    /* TODO */
    return FALSE;
}


static void amb_exit(mrp_plugin_t *plugin)
{
    data_t *ctx = plugin->data;
    mrp_list_hook_t *p, *n;

    /* for all subscribed properties, unsubscribe and free memory */

    mrp_list_foreach(&ctx->lua_properties, p, n) {
        dbus_property_watch_t *w =
                mrp_list_entry(p, dbus_property_watch_t, hook);

        destroy_prop(ctx, w);
    }

    global_ctx = NULL;
}

#define AMB_DESCRIPTION "A plugin for Automotive Message Broker D-Bus API."
#define AMB_HELP        "Access Automotive Message Broker."
#define AMB_VERSION     MRP_VERSION_INT(0, 0, 1)
#define AMB_AUTHORS     "Ismo Puustinen <ismo.puustinen@intel.com>"

static mrp_plugin_arg_t args[] = {
    MRP_PLUGIN_ARGIDX(ARG_AMB_DBUS_ADDRESS, STRING, "amb_address",
            "org.automotive.message.broker"),
    MRP_PLUGIN_ARGIDX(ARG_AMB_CONFIG_FILE, STRING, "config_file",
            "/etc/murphy/plugins/amb/config.lua"),
};


MURPHY_REGISTER_PLUGIN("amb",
                       AMB_VERSION, AMB_DESCRIPTION,
                       AMB_AUTHORS, AMB_HELP,
                       MRP_SINGLETON, amb_init, amb_exit,
                       args, MRP_ARRAY_SIZE(args),
                       NULL, 0, NULL, 0, NULL);
