/*
 * Copyright (c) 2014 Intel Corporation
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

#include <lualib.h>
#include <lauxlib.h>

#include <murphy/common.h>

#include <murphy/core/plugin.h>
#include <murphy/core/event.h>
#include <murphy/core/lua-bindings/murphy.h>
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/core/domain.h>

#include <murphy-db/mqi.h>
#include <murphy-db/mql.h>

#include <murphy/resource/client-api.h>
#include <murphy/resource/manager-api.h>

#include <murphy/resource/resource-set.h>
#include <murphy/resource/zone.h>
#include <murphy/resource/application-class.h>

#define PRIORITY_CLASS MRP_LUA_CLASS_SIMPLE(routing_sink_priority)
#define CONNID_ATTRIBUTE "connid"
#define APPID_ATTRIBUTE  "appid"

/* Listen for new resource sets. When one appears, check if it already has
 * attributes "sink" and "connection_id" set. If it has, the request has come
 * from a GAM-aware application, otherwise not. If not, tell GAM that a new
 * application has come around to ask for routing. */

typedef struct {
    char *name;
    mrp_funcbridge_t *get_sinks;
} priority_t;

typedef struct {
    mrp_mainloop_t *ml;
    mrp_context_t *mrp_ctx;

    char *zone;
    char *default_sink;
    mrp_json_t *mapping;
    char *app_default;
    char *playback_resource;
    char *recording_resource;

    mrp_htbl_t *pqs; /* "application_class" -> priority_t */

    mrp_event_watch_t *w;
    int events[4];
} immelmann_t;

typedef struct {
    char *name;
    mrp_list_hook_t hook;
} routing_target_t;

typedef struct {
    immelmann_t *ctx;
    uint32_t rset_id;
    char *resource;
} domain_data_t;

/* global context for Lua configuration */

static immelmann_t *global_ctx;

enum {
    ARG_ZONE,
    ARG_DEFAULT_SINK,
    ARG_APP_DEFAULT,
    ARG_APP_MAPPING,
    ARG_PLAYBACK_RESOURCE,
    ARG_RECORDING_RESOURCE,
};

enum {
    CREATED = 0,
    ACQUIRE,
    RELEASE,
    DESTROYED,
};

static int  priority_create(lua_State *);
static int  priority_getfield(lua_State *);
static int  priority_setfield(lua_State *);
static void priority_destroy(void *);

MRP_LUA_CLASS_DEF_SIMPLE (
    routing_sink_priority,
    priority_t,
    priority_destroy,
    MRP_LUA_METHOD_LIST (
        MRP_LUA_METHOD_CONSTRUCTOR  (priority_create)
    ),
    MRP_LUA_METHOD_LIST (
        MRP_LUA_OVERRIDE_CALL       (priority_create)
        MRP_LUA_OVERRIDE_GETFIELD   (priority_getfield)
        MRP_LUA_OVERRIDE_SETFIELD   (priority_setfield)
    )
);

#define BUFLEN 1024
static uint32_t get_node_id(immelmann_t *ctx, const char *name, bool sink)
{
    char cmdbuf[BUFLEN];
    int ret;
    mql_result_t *result = NULL;
    uint32_t id = 0;
    mqi_handle_t tx;

    MRP_UNUSED(ctx);

    tx = mqi_begin_transaction();

    /* FIXME: quote the string? */

    ret = snprintf(cmdbuf, BUFLEN, "SELECT id from %s where name = '%s'",
            sink ? "audio_manager_sinks" : "audio_manager_sources",
            name);

    if (ret < 0 || ret == BUFLEN)
        goto end;

    result = mql_exec_string(mql_result_rows, cmdbuf);

    if (!mql_result_is_success(result))
        goto end;

    if (mql_result_rows_get_row_count(result) != 1)
        goto end;

    id = mql_result_rows_get_unsigned(result, 0, 0); /* first column, row */

end:
    if (result)
        mql_result_free(result);

    mqi_commit_transaction(tx);
    return id;
}
#undef BUFLEN

static uint32_t get_source_id(immelmann_t *ctx, const char *source)
{
    return get_node_id(ctx, source, FALSE);
}

static uint32_t get_sink_id(immelmann_t *ctx, const char *sink)
{
    return get_node_id(ctx, sink, TRUE);
}


#define BUFLEN 1024
static bool is_sink_available(immelmann_t *ctx, const char *sink)
{
    char cmdbuf[BUFLEN];
    int ret;
    mql_result_t *result = NULL;
    bool available = FALSE;
    mqi_handle_t tx;

    MRP_UNUSED(ctx);

    /* check from the database table if GAM has provided this sink */

    tx = mqi_begin_transaction();

    /*
    a sink can be used if it's both visible and available:
        - "visible" means that the sink cannot be referenced from HMI.
        - "available" means that the sink cannot be used.
    */

    ret = snprintf(cmdbuf, BUFLEN, "SELECT * from audio_manager_sinks where name = '%s'", sink);

    if (ret < 0 || ret == BUFLEN)
        goto end;

    result = mql_exec_string(mql_result_rows, cmdbuf);

    if (!mql_result_is_success(result))
        goto end;

    if (mql_result_rows_get_row_count(result) == 1) {
        uint32_t sink_available = mql_result_rows_get_unsigned(result, 2, 0);
        uint32_t sink_visible = mql_result_rows_get_unsigned(result, 3, 0);

        /* currently there's a bug that makes AND not work in MQL */

        if (sink_available && sink_visible) {
            available = TRUE;
        }
    }

end:
    if (result)
        mql_result_free(result);

    mqi_commit_transaction(tx);
    return available;
}
#undef BUFLEN

static void create_priority_class(immelmann_t *ctx)
{
    lua_State *L = mrp_lua_get_lua_state();

    MRP_UNUSED(ctx);

    mrp_lua_create_object_class(L, PRIORITY_CLASS);
}

static mrp_list_hook_t *get_priorities_for_application_class(immelmann_t *ctx,
        const char *name)
{
    priority_t *prio = mrp_htbl_lookup(ctx->pqs, (char *) name);
    mrp_list_hook_t *priorities;
    int i;
    mrp_funcbridge_value_t ret;
    char t;
    mrp_funcbridge_value_t args[1] = { { .pointer = prio } };
    lua_State *L = mrp_lua_get_lua_state();
    char **arr;

    if (!prio->get_sinks)
        return NULL;

    priorities = mrp_allocz(sizeof(mrp_list_hook_t));

    if (!priorities) {
        goto error;
    }

    mrp_list_init(priorities);

    /* run the sink evaluation function and return the result */

    if (!mrp_funcbridge_call_from_c(L, prio->get_sinks, "o", args, &t,
        &ret)) {
        mrp_log_error("immelmann: failed to call priorities function (%s)",
                ret.string);
        mrp_free((void *) ret.string); /* error msg string or NULL */
        goto error;
    }

    if (t != MRP_FUNCBRIDGE_ARRAY) {
        mrp_log_error("immelmann: priorities with wrong return type (%c)",
                t);
        goto error;
    }

    if (!ret.array.type == MRP_FUNCBRIDGE_STRING) {
        mrp_log_error("immelmann: priorities aren't strings (%c)",
                ret.array.type);
        goto error;
    }

    arr = (char **) ret.array.items;

    for (i = 0; i < ret.array.nitem; i++) {

        routing_target_t *target = mrp_allocz(sizeof(routing_target_t));

        if (!target) {
            mrp_log_error("immelmann: out of memory");
            goto error;
        }

        mrp_list_init(&target->hook);

        if (!arr[i]) {
            mrp_log_error("immelmann: array missing elements");
            goto error;
        }

        target->name = mrp_strdup(arr[i]);
        if (!target->name) {
            mrp_log_error("immelmann: out of memory");
            mrp_free(target);
            goto error;
        }

        /* free the memory while we go */

        mrp_free(arr[i]);
        arr[i] = NULL;

        mrp_list_append(priorities, &target->hook);
    }

    mrp_free(ret.array.items);

    return priorities;

error:
    arr = (char **) ret.array.items;
    if (arr) {
        for (; i < ret.array.nitem; i++) {
            mrp_free(arr[i]);
        }

        mrp_free(arr);
    }

    if (priorities) {
        mrp_list_hook_t *p, *n;

        mrp_list_foreach(priorities, p, n) {
            routing_target_t *t = mrp_list_entry(p, typeof(*t), hook);

            mrp_list_delete(&t->hook);
            mrp_free(t->name);
            mrp_free(t);
        }

        mrp_free(priorities);
    }
    return NULL;
}

static const char *get_default_sink(immelmann_t *ctx, mrp_resource_set_t *rset)
{
    mrp_list_hook_t *pq = NULL;
    mrp_list_hook_t *p, *n;
    const char *name = NULL;

    /* get the priority list from Murphy configuration for this particular
     * application class */

    pq = get_priorities_for_application_class(ctx, rset->class.ptr->name);

    if (!pq)
        return NULL;

    /* go through the list while checking if the defined sinks are available */

    mrp_list_foreach(pq, p, n) {
        routing_target_t *t = mrp_list_entry(p, typeof(*t), hook);

        if (is_sink_available(ctx, t->name)) {
            name = t->name;
            break;
        }
    }

    /* free the data */
    mrp_list_foreach(pq, p, n) {
        routing_target_t *t = mrp_list_entry(p, typeof(*t), hook);
        mrp_list_delete(&t->hook);
        mrp_free(t->name);
        mrp_free(t);
    }
    mrp_free(pq);

    mrp_log_info("default sink: %s", name ? name : "null");

    return name;
}

static void connect_cb(int error, int retval, int narg, mrp_domctl_arg_t *args,
             void *user_data)
{
    uint32_t connid;
    domain_data_t *d = (domain_data_t *) user_data;
    mrp_resource_set_t *rset;

    MRP_UNUSED(narg);
    MRP_UNUSED(args);

    if (error || retval == 0) {
        mrp_log_error("immelmann: connect call to GAM failed: %d / %d", error,
                retval);
        goto end;
    }

    connid = retval;

    if (connid == 0) {
        mrp_log_error("immelmann: error doing the GAM connection");
        goto end;
    }

    rset = mrp_resource_set_find_by_id(d->rset_id);

    if (!rset) {
        mrp_log_error("immelmann: no resource set matching id (%u)", d->rset_id);
        goto end;
    }

    mrp_log_info("immelmann: got connection id %u for resource set %u",
                 connid, d->rset_id);

end:
    mrp_free(d->resource);
    mrp_free(d);

    /* TODO: what to do to the resource set in the error case? */
}

static bool register_sink_with_gam(immelmann_t *ctx, mrp_resource_set_t *rset,
        const char *appid)
{
    const char *source;
    const char *sink;
    uint32_t sink_id, source_id, rset_id;
    mrp_domctl_arg_t args[3];
    domain_data_t *d;

    /* map the appid to sources as defined in configuration */

    if (ctx->mapping == NULL ||
            !mrp_json_get_string(ctx->mapping, appid, &source))
        source = ctx->app_default;

    /* ask GAM for the connection via control interface */

    sink = get_default_sink(ctx, rset);

    if (!sink) {
        mrp_log_error("immelmann: error finding default sink, using global default");
        sink = ctx->default_sink;
    }

    rset_id = mrp_get_resource_set_id(rset);
    source_id = get_source_id(ctx, source);
    sink_id = get_sink_id(ctx, sink);

    mrp_log_info("immelmann: register rset %u with GAM! (%s(%u) -> %s(%u))",
            rset_id, source, source_id, sink, sink_id);

    /*
        calling GAM:
            domain: "audio-manager", function: "connect", source_id, sink_id
            return value: connection id, 0 as error
    */

    args[0].type = MRP_DOMCTL_UINT16;
    args[0].u16 = source_id;

    args[1].type = MRP_DOMCTL_UINT16;
    args[1].u16 = sink_id;

    args[2].type = MRP_DOMCTL_UINT32;
    args[2].u32 = rset_id;

    /* TODO: do proper error handling */

    d = mrp_allocz(sizeof(domain_data_t));

    if (!d) {
        mrp_log_error("immelmann: memory allocation error");
        return false;
    }

    /* TODO: resource names from config */
    d->resource = mrp_strdup(ctx->playback_resource);
    if (!d->resource) {
        mrp_log_error("immelmann: memory allocation error");
        mrp_free(d);
        return false;
    }

    d->ctx = ctx;
    d->rset_id = rset->id; /* use id to escape a race condition */

    if (!mrp_invoke_domain(ctx->mrp_ctx, "audio-manager", "connect", 3, args,
            connect_cb, d)) {
        mrp_log_error("immmelmann: failed to send connect request to GAM");
        mrp_free(d->resource);
        mrp_free(d);
        return false;
    }

    return true;
}

static void resource_set_event(mrp_event_watch_t *w, int id, mrp_msg_t *event_data,
        void *user_data)
{
    immelmann_t *ctx = (immelmann_t *) user_data;
    uint32_t rset_id;
    uint16_t tag = MRP_RESOURCE_TAG_RSET_ID;

    MRP_UNUSED(w);

    mrp_msg_get(event_data, MRP_MSG_TAG_UINT32(tag, &rset_id), NULL);

    mrp_log_info("immelmann: resource set event '%d' for '%u' received!", id, rset_id);

    if (id == ctx->events[CREATED]) {
        mrp_resource_set_t *rset = mrp_resource_set_find_by_id(rset_id);

        mrp_log_info("immelmann: Resource set %u (%p) was created", rset_id, rset);
    }
    else if (id == ctx->events[ACQUIRE]) {
        mrp_resource_set_t *rset = mrp_resource_set_find_by_id(rset_id);
        void *iter = NULL;
        mrp_resource_t *resource;
        mrp_resource_t *audio_playback = NULL;
        mrp_resource_t *audio_recording = NULL;
        const char *name;

        if (!rset)
            return;

        while ((resource = mrp_resource_set_iterate_resources(rset, &iter))) {

            name = mrp_resource_get_name(resource);

            if (strcmp(name, ctx->playback_resource) == 0)
                audio_playback = resource;

            else if (strcmp(name, ctx->recording_resource) == 0)
                audio_recording = resource;

            if (audio_playback && audio_recording)
                break;
        }

        if (audio_playback) {
            mrp_attr_t *app_id; /* the application itself */
            mrp_attr_t *conn_id;

            app_id = mrp_resource_set_get_attribute_by_name(rset,
                    ctx->playback_resource, APPID_ATTRIBUTE);
            conn_id = mrp_resource_set_get_attribute_by_name(rset,
                    ctx->playback_resource, CONNID_ATTRIBUTE);

            if (!app_id || !conn_id) {
                /* what is this? */
                mrp_log_error("immelmann: source or conn_id attributes not defined!");
            }
            else if (conn_id->type != mqi_integer ||
                    app_id->type != mqi_string) {
                mrp_log_error("immelmann: appid or connid types don't match!");
            }
            else if (conn_id->value.integer < 1) {
                /* this connection is not yet managed by GAM */

                register_sink_with_gam(ctx, rset, app_id->value.string);
            }
        }

        /* TODO: think properly about hos to handle audio recording */

        mrp_log_info("immelmann: Resource set %u (%p) was acquired", rset_id, rset);
    }
    else if (id == ctx->events[RELEASE]) {
        mrp_resource_set_t *rset = mrp_resource_set_find_by_id(rset_id);

        mrp_log_info("immelmann: Resource set %u (%p) was released", rset_id, rset);
    }
    else if (id == ctx->events[DESTROYED]) {
        mrp_log_info("immelmann: Resource set %u was destroyed", rset_id);
    }
}


static int priority_create(lua_State *L)
{
    priority_t *prio;
    size_t fldnamlen;
    const char *fldnam;
    char *name = NULL;
    mrp_funcbridge_t *get_sinks = NULL;
    mrp_list_hook_t *priority_queue;
    immelmann_t *ctx = global_ctx;

    MRP_LUA_ENTER;

    priority_queue = mrp_allocz(sizeof(mrp_list_hook_t));

    mrp_list_init(priority_queue);

    /*
        general_pq = {
            "headset", "bluetooth", "speakers"
        }

        sink_routing_priority {
            application_class = "player",
            priority_queue = general_pq
        }
    */

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {

        if (strncmp(fldnam, "application_class", fldnamlen) == 0) {
            name = mrp_strdup(luaL_checkstring(L, -1));
        }
        else if (strncmp(fldnam, "priority_queue", fldnamlen) == 0) {
            if (lua_isfunction(L, -1)) {
                /* store the function to a function pointer */
                get_sinks = mrp_funcbridge_create_luafunc(L, -1);
            }
        }
        else {
            mrp_log_error("immelmann: unknown field");
        }
    }

    if (!name)
        luaL_error(L, "missing or wrong application_class field");

    /* FIXME: check that there is an application class definition for 'name'? */

    prio = (priority_t *) mrp_lua_create_object(L, PRIORITY_CLASS, name, 0);

    if (!prio)
        luaL_error(L, "invalid or duplicate application_class '%s'", name);
    else {
        prio->name = name;
        prio->get_sinks = get_sinks;
        mrp_log_info("immelmann: application class priority object '%s' created", name);
    }

    if (ctx) {
        mrp_htbl_insert(ctx->pqs, name, prio);
    }

    MRP_LUA_LEAVE(1);
}

static int priority_getfield(lua_State *L)
{
    MRP_LUA_ENTER;

    /* TODO */
    lua_pushnil(L);

    MRP_LUA_LEAVE(1);
}

static int priority_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "TODO: add dynamic modification of priorities");

    MRP_LUA_LEAVE(0);
}

static void priority_destroy(void *data)
{
    priority_t *prio = (priority_t *) data;
    mrp_list_hook_t *p, *n;

    MRP_LUA_ENTER;

    mrp_free(prio->name);

    mrp_funcbridge_unref(mrp_lua_get_lua_state(), prio->get_sinks);
    prio->get_sinks = NULL;
    prio->name = NULL;

    MRP_LUA_LEAVE_NOARG;
}

static int plugin_init(mrp_plugin_t *plugin)
{
    immelmann_t *ctx;
    ctx = mrp_allocz(sizeof(immelmann_t));
    mrp_event_mask_t mask = 0;
    mrp_htbl_config_t conf;

    mrp_reset_event_mask(&mask);

    if (!ctx)
        goto error;

    ctx->zone = mrp_strdup(plugin->args[ARG_ZONE].str);
    ctx->default_sink = mrp_strdup(plugin->args[ARG_DEFAULT_SINK].str);
    ctx->app_default = mrp_strdup(plugin->args[ARG_APP_DEFAULT].str);
    ctx->playback_resource = mrp_strdup(plugin->args[ARG_PLAYBACK_RESOURCE].str);
    ctx->recording_resource = mrp_strdup(plugin->args[ARG_RECORDING_RESOURCE].str);

    ctx->mapping = plugin->args[ARG_APP_MAPPING].obj.json;

    if (!ctx->zone || !ctx->default_sink)
        goto error;

    ctx->ml = plugin->ctx->ml;
    ctx->mrp_ctx = plugin->ctx;
    plugin->data = ctx;

    global_ctx = ctx;

    /* subscribe to the events coming from resource library */

    mrp_add_named_event(&mask, MURPHY_RESOURCE_EVENT_CREATED);
    mrp_add_named_event(&mask, MURPHY_RESOURCE_EVENT_ACQUIRE);
    mrp_add_named_event(&mask, MURPHY_RESOURCE_EVENT_RELEASE);
    mrp_add_named_event(&mask, MURPHY_RESOURCE_EVENT_DESTROYED);

    ctx->events[CREATED] = mrp_get_event_id(MURPHY_RESOURCE_EVENT_CREATED, 1);
    ctx->events[ACQUIRE] = mrp_get_event_id(MURPHY_RESOURCE_EVENT_ACQUIRE, 1);
    ctx->events[RELEASE] = mrp_get_event_id(MURPHY_RESOURCE_EVENT_RELEASE, 1);
    ctx->events[DESTROYED] = mrp_get_event_id(MURPHY_RESOURCE_EVENT_DESTROYED,
            1);

    mrp_add_event(&mask, ctx->events[CREATED]);
    mrp_add_event(&mask, ctx->events[ACQUIRE]);
    mrp_add_event(&mask, ctx->events[RELEASE]);
    mrp_add_event(&mask, ctx->events[DESTROYED]);

    conf.comp = mrp_string_comp;
    conf.hash = mrp_string_hash;
    conf.free = NULL;
    conf.nbucket = 0;
    conf.nentry = 10;

    ctx->pqs = mrp_htbl_create(&conf);

    if (!ctx->pqs)
        goto error;

    ctx->w = mrp_add_event_watch(&mask, resource_set_event, ctx);
    if (!ctx->w)
        goto error;

    create_priority_class(ctx);

    mrp_log_info("Launched the Murphy Immelmann plugin!");
    return TRUE;

error:
    mrp_log_error("Error starting Murphy Immelmann plugin!");

    if (ctx->w)
        mrp_del_event_watch(ctx->w);

    if (ctx->pqs)
        mrp_htbl_destroy(ctx->pqs, FALSE);

    mrp_free(ctx->default_sink);
    mrp_free(ctx->app_default);
    mrp_free(ctx->playback_resource);
    mrp_free(ctx->recording_resource);
    mrp_free(ctx->zone);
    if (ctx->mapping)
        mrp_json_unref(ctx->mapping);
    mrp_free(ctx);

    global_ctx = NULL;

    return FALSE;
}

static void plugin_exit(mrp_plugin_t *plugin)
{
    immelmann_t *ctx = (immelmann_t *) plugin->data;

    if (ctx->w)
        mrp_del_event_watch(ctx->w);

    if (ctx->pqs)
        mrp_htbl_destroy(ctx->pqs, FALSE);

    mrp_free(ctx->default_sink);
    mrp_free(ctx->zone);
    mrp_free(ctx);

    global_ctx = NULL;
}

#define DEFAULT_ZONE                "driver"
#define DEFAULT_SINK                "speakers"
#define DEFAULT_APP_DEFAULT         "icoApplication"
#define DEFAULT_APP_MAPPING         "{}"
#define DEFAULT_PLAYBACK_RESOURCE   "audio_playback"
#define DEFAULT_RECORDING_RESOURCE  "audio_recording"

#define PLUGIN_DESCRIPTION "Plugin to add PA streams to GAM (with resource support)"
#define PLUGIN_HELP        "Help coming later."
#define PLUGIN_AUTHORS     "Ismo Puustinen <ismo.puustinen@intel.com>"
#define PLUGIN_VERSION     MRP_VERSION_INT(0, 0, 1)

static mrp_plugin_arg_t plugin_args[] = {
    MRP_PLUGIN_ARGIDX(ARG_ZONE, STRING, "zone", DEFAULT_ZONE),
    MRP_PLUGIN_ARGIDX(ARG_DEFAULT_SINK, STRING, "default_sink", DEFAULT_SINK),
    MRP_PLUGIN_ARGIDX(ARG_APP_DEFAULT, STRING, "app_default", DEFAULT_APP_DEFAULT),
    MRP_PLUGIN_ARGIDX(ARG_APP_MAPPING, OBJECT, "app_mapping", DEFAULT_APP_MAPPING),
    MRP_PLUGIN_ARGIDX(ARG_PLAYBACK_RESOURCE, STRING, "playback_resource", DEFAULT_PLAYBACK_RESOURCE),
    MRP_PLUGIN_ARGIDX(ARG_RECORDING_RESOURCE, STRING, "recording_resource", DEFAULT_RECORDING_RESOURCE),
};

MURPHY_REGISTER_PLUGIN("immelmann",
                       PLUGIN_VERSION, PLUGIN_DESCRIPTION, PLUGIN_AUTHORS,
                       PLUGIN_HELP, MRP_SINGLETON, plugin_init, plugin_exit,
                       plugin_args, MRP_ARRAY_SIZE(plugin_args),
                       NULL, 0,
                       NULL, 0,
                       NULL);
