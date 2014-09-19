/*
 * Copyright (c) 2014, Intel Corporation
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

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <lualib.h>
#include <lauxlib.h>

#include <murphy/common.h>
#include <murphy/core.h>
#include <murphy/common/debug.h>
#include <murphy/core/plugin.h>
#include <murphy/core/console.h>
#include <murphy/core/event.h>
#include <murphy/core/context.h>
#include <murphy/core/domain.h>
#include <murphy/core/lua-bindings/murphy.h>
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>

#include <murphy-db/mqi.h>
#include <murphy-db/mql.h>
#include <murphy-db/mql-result.h>

#include <murphy/resource/config-api.h>
#include <murphy/resource/client-api.h>
#include <murphy/resource/manager-api.h>
#include <murphy/resource/protocol.h>
#include <murphy/resource/resource-set.h>
#include <murphy/resource/zone.h>
#include <murphy/resource/application-class.h>

#include "plugin-gam-resource-manager.h"
#include "backend.h"
#include "source.h"
#include "sink.h"
#include "usecase.h"


/* gam_connect is the part of the GAM plugin that creates connections within
 * the GAM for those resource sets which are not created through GAM. This means
 * non-GAM-aware applications such as PulseAudio clients. Also, gam_connect
 * maps clients to sources and finds correct sinks for various application
 * classes. */

#define PRIORITY_CLASS MRP_LUA_CLASS_SIMPLE(routing_sink_priority)
#define CONNID_ATTRIBUTE "connid"
#define APPID_ATTRIBUTE  "appid"

#define DEFAULT_ZONE                "driver"
#define DEFAULT_SINK                "speakers"
#define DEFAULT_APP_DEFAULT         "icoApplication"
#define DEFAULT_APP_MAPPING         "{}"
#define DEFAULT_PLAYBACK_RESOURCE   "audio_playback"
#define DEFAULT_RECORDING_RESOURCE  "audio_recording"

/* Listen for new resource sets. When one appears, check if it already has
 * attributes APPID_ATTRIBUTE and CONNID_ATTRIBUTE set. If it has, the request
 * has come from a GAM-aware application, otherwise not. If not, tell GAM that a
 * new application has come around to ask for routing. */

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
} gam_connect_t;

typedef struct {
    char *name;
    mrp_list_hook_t hook;
} routing_target_t;

typedef struct {
    gam_connect_t *ctx;
    uint32_t rset_id;
    char *resource;
} domain_data_t;

/* global context for Lua configuration */

static gam_connect_t *global_ctx;

enum {
    ARG_ZONE,
    ARG_DEFAULT_SINK,
    ARG_APP_DEFAULT,
    ARG_APP_MAPPING,
    ARG_PLAYBACK_RESOURCE,
    ARG_RECORDING_RESOURCE,
    ARG_MURPHY_CONNECTS,
    ARG_CONFDIR,
    ARG_PREFIX,
    ARG_DECISION_NAMES,
    ARG_MAX_ACTIVE,
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
static uint32_t get_node_id(gam_connect_t *ctx, const char *name, bool sink)
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

static uint32_t get_source_id(gam_connect_t *ctx, const char *source)
{
    return get_node_id(ctx, source, FALSE);
}

static uint32_t get_sink_id(gam_connect_t *ctx, const char *sink)
{
    return get_node_id(ctx, sink, TRUE);
}


#define BUFLEN 1024
static bool is_sink_available(gam_connect_t *ctx, const char *sink)
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

static void create_priority_class(gam_connect_t *ctx)
{
    lua_State *L = mrp_lua_get_lua_state();

    MRP_UNUSED(ctx);

    mrp_lua_create_object_class(L, PRIORITY_CLASS);
}

static mrp_list_hook_t *get_priorities_for_application_class(gam_connect_t *ctx,
        const char *name)
{
    priority_t *prio = mrp_htbl_lookup(ctx->pqs, (char *) name);
    mrp_list_hook_t *priorities;
    int i = 0; /* current value is used also in error handling */
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
        mrp_log_error("gam_connect: failed to call priorities function (%s)",
                ret.string);
        mrp_free((void *) ret.string); /* error msg string or NULL */
        goto error;
    }

    if (t != MRP_FUNCBRIDGE_ARRAY) {
        mrp_log_error("gam_connect: priorities with wrong return type (%c)",
                t);
        goto error;
    }

    if (!ret.array.type == MRP_FUNCBRIDGE_STRING) {
        mrp_log_error("gam_connect: priorities aren't strings (%c)",
                ret.array.type);
        goto error;
    }

    arr = (char **) ret.array.items;

    for (; i < ret.array.nitem; i++) {

        routing_target_t *target = mrp_allocz(sizeof(routing_target_t));

        if (!target) {
            mrp_log_error("gam_connect: out of memory");
            goto error;
        }

        mrp_list_init(&target->hook);

        if (!arr[i]) {
            mrp_log_error("gam_connect: array missing elements");
            goto error;
        }

        target->name = mrp_strdup(arr[i]);
        if (!target->name) {
            mrp_log_error("gam_connect: out of memory");
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

static const char *get_default_sink(gam_connect_t *ctx, mrp_resource_set_t *rset)
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
            name = mrp_strdup(t->name);
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

    mrp_log_info("gam_connect: default sink: %s", name ? name : "null");

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
        mrp_log_error("gam_connect: connect call to GAM failed: %d / %d", error,
                retval);
        goto end;
    }

    connid = retval;

    if (connid == 0) {
        mrp_log_error("gam_connect: error doing the GAM connection");
        goto end;
    }

    rset = mrp_resource_set_find_by_id(d->rset_id);

    if (!rset) {
        mrp_log_error("gam_connect: no resource set matching id (%u)", d->rset_id);
        goto end;
    }

    mrp_log_info("gam_connect: got connection id %u for resource set %u",
                 connid, d->rset_id);

end:
    mrp_free(d->resource);
    mrp_free(d);

    /* TODO: what to do to the resource set in the error case? */
}

static bool register_sink_with_gam(gam_connect_t *ctx, mrp_resource_set_t *rset,
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
        mrp_log_error("gam_connect: error finding default sink, using global default");
        sink = mrp_strdup(ctx->default_sink);
    }

    rset_id = mrp_get_resource_set_id(rset);
    source_id = get_source_id(ctx, source);
    sink_id = get_sink_id(ctx, sink);

    mrp_debug("gam_connect: register rset %u with GAM! (%s(%u) -> %s(%u))",
            rset_id, source, source_id, sink, sink_id);

    mrp_free(sink);

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

    d = mrp_allocz(sizeof(domain_data_t));

    if (!d) {
        mrp_log_error("gam_connect: memory allocation error");
        return false;
    }

    /* TODO: resource names from config */
    d->resource = mrp_strdup(ctx->playback_resource);
    if (!d->resource) {
        mrp_log_error("gam_connect: memory allocation error");
        mrp_free(d);
        return false;
    }

    d->ctx = ctx;
    d->rset_id = rset->id; /* use id to escape a race condition */

    if (!mrp_invoke_domain(ctx->mrp_ctx, "audio-manager", "connect", 3, args,
            connect_cb, d)) {
        mrp_log_error("gam_connect: failed to send connect request to GAM");
        mrp_free(d->resource);
        mrp_free(d);
        return false;
    }

    return true;
}

static void resource_set_event(mrp_event_watch_t *w, int id, mrp_msg_t *event_data,
        void *user_data)
{
    gam_connect_t *ctx = (gam_connect_t *) user_data;
    uint32_t rset_id;
    uint16_t tag = MRP_RESOURCE_TAG_RSET_ID;

    MRP_UNUSED(w);

    mrp_msg_get(event_data, MRP_MSG_TAG_UINT32(tag, &rset_id), NULL);

    mrp_log_info("gam_connect: resource set event '%d' for '%u' received!", id, rset_id);

    if (id == ctx->events[CREATED]) {
        mrp_resource_set_t *rset = mrp_resource_set_find_by_id(rset_id);

        mrp_debug("gam_connect: Resource set %u (%p) was created", rset_id, rset);
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
                mrp_log_error("gam_connect: source or conn_id attributes not defined!");
            }
            else if (conn_id->type != mqi_integer ||
                    app_id->type != mqi_string) {
                mrp_log_error("gam_connect: appid or connid types don't match!");
            }
            else if (conn_id->value.integer < 1) {
                /* this connection is not yet managed by GAM */

                register_sink_with_gam(ctx, rset, app_id->value.string);
            }
        }

        /* TODO: think properly about how to handle audio recording */

        mrp_debug("gam_connect: Resource set %u (%p) was acquired", rset_id, rset);
    }
    else if (id == ctx->events[RELEASE]) {
        mrp_resource_set_t *rset = mrp_resource_set_find_by_id(rset_id);

        mrp_debug("gam_connect: Resource set %u (%p) was released", rset_id, rset);
    }
    else if (id == ctx->events[DESTROYED]) {
        mrp_debug("gam_connect: Resource set %u was destroyed", rset_id);
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
    gam_connect_t *ctx = global_ctx;

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
            mrp_log_error("gam_connect: unknown field");
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
        mrp_debug("gam_connect: application class priority object '%s' created", name);
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

static int gam_connect_init(mrp_plugin_t *plugin)
{
    gam_connect_t *ctx;
    mrp_event_mask_t mask = 0;
    mrp_htbl_config_t conf;

    if (!plugin->args[ARG_MURPHY_CONNECTS].bln) {
        return TRUE;
    }

    ctx = mrp_allocz(sizeof(gam_connect_t));

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

    mrp_log_info("Ready to initiate Genivi Audio Manager connections.");
    return TRUE;

error:
    mrp_log_error("Unable to do Genivi Audio Manager connections!");

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

static void gam_connect_exit(mrp_plugin_t *plugin)
{
    gam_connect_t *ctx;

    if (!plugin->args[ARG_MURPHY_CONNECTS].bln) {
        return;
    }

    ctx = (gam_connect_t *) plugin->data;

    if (ctx->w)
        mrp_del_event_watch(ctx->w);

    if (ctx->pqs)
        mrp_htbl_destroy(ctx->pqs, FALSE);

    mrp_free(ctx->default_sink);
    mrp_free(ctx->zone);
    mrp_free(ctx);

    global_ctx = NULL;
}

/* control */

#define SOURCETBL "audio_manager_sources"
#define SINKTBL   "audio_manager_sinks"

typedef struct gamctl_s gamctl_t;
typedef struct route_s  route_t;


/*
 * plugin context/state
 */

struct gamctl_s {
    mrp_plugin_t          *self;         /* us, this plugin */
    mrp_resource_client_t *rsc;          /* resource client */
    mrp_htbl_t            *rstbl;        /* resource sets */
    uint32_t               seq;          /* request sequence number */
    uint32_t               sourcef;      /* source table fingerprint (stamp) */
    uint32_t               sinkf;        /* sink table fingerprint (stamp) */
    route_t               *routes;       /* routing table */
    size_t                 nroute;       /* number of routing entries */
    mrp_deferred_t        *recalc;       /* deferred resource recalculation */
};


/*
 * a route from source to sink
 */

struct route_s {
    char     *source;                    /* source name */
    char     *sink;                      /* sink name */
    uint16_t *hops;                      /* routing hops */
    size_t    nhop;                      /* number of hops */
};


/*
 * a source or sink node to id mapping
 */

typedef struct {
    const char *name;                    /* source/sink name */
    uint16_t    id;                      /* source/sink id */
} node_t;


/*
 * a (predefined) routing path
 */

typedef struct {
    size_t  nhop;                        /* number of hops */
    char   *hops[32];                    /* hop names */
} path_t;



static int  resctl_init(gamctl_t *gam);
static void resctl_exit(gamctl_t *gam);
static int  domctl_init(gamctl_t *gam);
static void domctl_exit(gamctl_t *gam);
static void gamctl_exit(mrp_plugin_t *plugin);

static int gamctl_route_cb(int narg, mrp_domctl_arg_t *args,
                           uint32_t *nout, mrp_domctl_arg_t *outs,
                           void *user_data);
static int gamctl_disconnect_cb(int narg, mrp_domctl_arg_t *args,
                                uint32_t *nout, mrp_domctl_arg_t *outs,
                                void *user_data);

static char *route_dump(gamctl_t *gam, char *buf, size_t size,
                        route_t *r, int verbose);


/*
 * hardwired sources, sinks and routing paths
 */

static node_t sources[] = {
    { "wrtApplication", 0 },
    { "icoApplication", 0 },
    { "phoneSource"   , 0 },
    { "radio"         , 0 },
    { "microphone"    , 0 },
    { "navigator"     , 0 },
    { "gw1Source"     , 0 },
    { "gw2Source"     , 0 },
    { "gw3Source"     , 0 },
    { "gw4Source"     , 0 },
    { "gw5Source"     , 0 },
    { "gw6Source"     , 0 },
    { NULL, 0 }
};

static node_t sinks[] = {
    { "btHeadset"       , 0 },
    { "usbHeadset"      , 0 },
    { "speakers"        , 0 },
    { "wiredHeadset"    , 0 },
    { "phoneSink"       , 0 },
    { "voiceRecognition", 0 },
    { "gw1Sink"         , 0 },
    { "gw2Sink"         , 0 },
    { "gw3Sink"         , 0 },
    { "gw4Sink"         , 0 },
    { "gw5Sink"         , 0 },
    { "gw6Sink"         , 0 },
    { NULL, 0 }
};

static path_t paths[] = {
    { 2, { "wrtApplication", "btHeadset"  } },
    { 2, { "wrtApplication", "usbHeadset" } },
    { 4, { "wrtApplication", "gw2Sink", "gw2Source", "speakers"     } },
    { 4, { "wrtApplication", "gw2Sink", "gw2Source", "wiredHeadset" } },

    { 2, { "icoApplication", "btHeadset"  } },
    { 2, { "icoApplication", "usbHeadset" } },
    { 4, { "icoApplication", "gw1Sink", "gw1Source", "speakers"     } },
    { 4, { "icoApplication", "gw1Sink", "gw1Source", "wiredHeadset" } },

    { 2, { "phoneSource", "btHeadset"  } },
    { 2, { "phoneSource", "usbHeadset" } },
    { 4, { "phoneSource", "gw1Sink", "gw1Source", "speakers"     } },
    { 4, { "phoneSource", "gw1Sink", "gw1Source", "wiredHeadset" } },

    { 4, { "radio", "gw3Sink", "gw3Source", "btHeadset"  } },
    { 4, { "radio", "gw3Sink", "gw3Source", "usbHeadset" } },
    { 2, { "radio", "speakers"     } },
    { 2, { "radio", "wiredHeadset" } },

    { 4, { "microphone", "gw6Sink", "gw6Source", "phoneSink"        } },
    { 4, { "microphone", "gw5Sink", "gw5Source", "voiceRecognition" } },

    { 4, { "navigator", "gw4Sink", "gw4Source", "speakers"     } },
    { 4, { "navigator", "gw4Sink", "gw4Source", "wiredHeadset" } },
};


static uint16_t node_id(node_t *nodes, const char *name)
{
    node_t *node;

    for (node = nodes; node->name != NULL; node++)
        if (!strcmp(node->name, name))
            return node->id;

    return 0;
}


static uint16_t source_id(const char *name)
{
    return node_id(sources, name);
}


static uint16_t sink_id(const char *name)
{
    return node_id(sinks, name);
}


static int exec_mql(mql_result_type_t type, mql_result_t **resultp,
                    const char *format, ...)
{
    mql_result_t *r;
    char          buf[4096];
    va_list       ap;
    int           success, n;

    va_start(ap, format);
    n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (n < (int)sizeof(buf)) {
        r       = mql_exec_string(type, buf);
        success = (r == NULL || mql_result_is_success(r));

        if (resultp != NULL) {
            *resultp = r;
            return success;
        }
        else {
            mql_result_free(r);
            return success;
        }
    }
    else {
        if (resultp != NULL)
            *resultp = NULL;

        return FALSE;
    }
}


static void reset_nodes(node_t *nodes)
{
    node_t *node;

    for (node = nodes; node->name != NULL; node++)
        node->id = 0;
}


static int resolve_nodes(const char *type, node_t *nodes,
                         const char *tbl, uint32_t *age)
{
    mql_result_t *r = NULL;
    uint32_t      h, stamp;
    node_t       *node;
    uint16_t      nrow, i, id;
    const char   *name;

    if ((h = mqi_get_table_handle((char *)tbl)) == MQI_HANDLE_INVALID)
        return FALSE;

    if (*age >= (stamp = mqi_get_table_stamp(h)))
        return TRUE;

    if (!exec_mql(mql_result_rows, &r, "select id,name from %s", tbl))
        return FALSE;

    reset_nodes(nodes);

    if (r == NULL)
        return FALSE;

    nrow = mql_result_rows_get_row_count(r);

    if (nrow == 0)
        return FALSE;

    if (mql_result_rows_get_row_column_type(r, 0) != mqi_integer ||
        mql_result_rows_get_row_column_type(r, 1) != mqi_string) {
        mrp_log_error("Invalid column types for table '%s'.", tbl);
        return FALSE;
    }

    for (i = 0; i < nrow; i++) {
        id   = mql_result_rows_get_integer(r, 0, i);
        name = mql_result_rows_get_string(r, 1, i, NULL, 0);

        for (node = nodes; node->name != NULL; node++) {
            if (!strcmp(node->name, name)) {
                node->id = id;
                mrp_debug("%s '%s' has now id %u", type, name, id);
                break;
            }
        }

        if (node->name == NULL)
            mrp_debug("(unused) %s '%s' has id %u", type, name, id);
    }

    mql_result_free(r);

    *age = stamp;

    return TRUE;
}


static int resolve_routes(gamctl_t *gam)
{
    const char *type, *node;
    char        route[4096];
    route_t    *r;
    path_t     *p;
    uint16_t    id;
    size_t      i;
    int         incomplete;

    for (r = gam->routes, p = paths; r->nhop; r++, p++) {
        incomplete = FALSE;
        for (i = 0; i < r->nhop; i++) {
            node = p->hops[i];

            if (i & 0x1) {
                type = "sink";
                id   = sink_id(node);
            }
            else {
                type = "source";
                id   = source_id(node);
            }

            if (!id) {
                mrp_log_warning("Unresolved %s '%s'.",type, node);
                incomplete = TRUE;
            }

            r->hops[i] = id;
        }

        if (!incomplete)
            mrp_debug("Resolved route: %s",
                         route_dump(gam, route, sizeof(route), r, TRUE));
        else
            mrp_log_warning("Unresolvable route: %s",
                            route_dump(gam, route, sizeof(route), r, TRUE));
    }

    return TRUE;
}


static int connid_cmp(const void *key1, const void *key2)
{
    return key2 - key1;
}


static uint32_t connid_hash(const void *key)
{
    uint16_t connid = (uint16_t)(ptrdiff_t)key;

    return connid;
}


static int resctl_init(gamctl_t *gam)
{
    mrp_htbl_config_t hcfg;

    mrp_clear(&hcfg);
    hcfg.comp  = connid_cmp;
    hcfg.hash  = connid_hash;
    hcfg.free  = NULL;

    gam->rstbl = mrp_htbl_create(&hcfg);

    if (gam->rstbl == NULL)
        return FALSE;

    gam->seq = 1;
    gam->rsc = mrp_resource_client_create("genivi-audio-manager", gam);

    if (gam->rsc == NULL) {
        mrp_log_error("Failed to create Genivi Audio Manager resource client.");
        return FALSE;
    }
    else {
        mrp_log_info("Created Genivi Audio Manager resource client.");
        return TRUE;
    }
}


static void resctl_event_cb(uint32_t seq, mrp_resource_set_t *set,
                            void *user_data)
{
    gamctl_t *gam = (gamctl_t *)user_data;

    MRP_UNUSED(seq);
    MRP_UNUSED(set);
    MRP_UNUSED(gam);
}


static mrp_resource_set_t *resctl_create(gamctl_t *gam, uint16_t source,
                                         uint16_t sink, uint16_t connid,
                                         uint16_t connno)
{
    mrp_resource_client_t *rsc    = gam->rsc;
    uint32_t               seq    = gam->seq++;
    const char            *zone   = "driver";
    const char            *cls    = "player";
    const char            *play   = "audio_playback";
    uint32_t               prio   = 0;
    bool                   ar     = false;
    bool                   nowait = false;
    mrp_resource_set_t    *set;
    mrp_attr_t             attrs[16], *attrp;

    mrp_log_info("Creating resource set for Genivi Audio Manager connection "
                 "%u (%u -> %u, #%u).", connid, source, sink, connno);

    set = mrp_resource_set_create(rsc, ar, nowait, prio, resctl_event_cb, gam);

    if (set == NULL) {
        mrp_log_error("Failed to create resource set for Genivi Audio Manager "
                      "connection %u (%u -> %u).", connid, source, sink);
        goto fail;
    }

    attrs[0].type          = mqi_integer;
    attrs[0].name          = "source_id";
    attrs[0].value.integer = source;
    attrs[1].type          = mqi_integer;
    attrs[1].name          = "sink_id";
    attrs[1].value.integer = sink;
    attrs[2].type          = mqi_integer;
    attrs[2].name          = "connid";
    attrs[2].value.integer = connid;
    attrs[3].type          = mqi_integer;
    attrs[3].name          = "connno";
    attrs[3].value.integer = connno;
    attrs[4].name          = NULL;

    attrp = &attrs[0];

    if (mrp_resource_set_add_resource(set, play, true, attrp, true) < 0) {
        mrp_log_error("Failed to add resource %s to Genivi Audio "
                      "Manager resource set.", play);
        goto fail;
    }

    if (mrp_application_class_add_resource_set(cls, zone, set, seq) != 0) {
        mrp_log_error("Failed to add Genivi Audio Manager resource set "
                      "to application class %s in zone %s.", cls, zone);
        goto fail;
    }

    if (mrp_htbl_insert(gam->rstbl, (void *)(ptrdiff_t)connid, set))
        return set;
    else
        mrp_log_error("Failed to associate resource set with connection %u.",
                      connid);
    /* fallthru */

 fail:
    if (set != NULL)
        mrp_resource_set_destroy(set);

    return NULL;
}


static int resctl_update(gamctl_t *gam, uint32_t rsetid,
                         uint16_t source, uint16_t sink, uint16_t conn)
{
    static int srcidx = -1, sinkidx = -1, connidx = -1;

    mrp_resource_set_t *rset  = mrp_resource_set_find_by_id(rsetid);
    mrp_attr_t         *attrs = NULL, *a;
    int                 i, status;

    MRP_UNUSED(gam);

    if (rset == NULL) {
        mrp_log_error("Failed to update resource set, can't find set 0x%x.",
                      rsetid);
        return FALSE;
    }

    attrs = mrp_resource_set_read_all_attributes(rset, "audio_playback", 0,NULL);

    if (attrs == NULL) {
        mrp_log_error("Failed to read resource set attribute list.");
        return FALSE;
    }

    if (srcidx >= 0 && sinkidx >= 0 && connidx >= 0) {
        attrs[srcidx].value.integer  = source;
        attrs[sinkidx].value.integer = sink;
        attrs[connidx].value.integer = conn;
    }
    else {
        for (a = attrs, i = 0; a->name != NULL; a++, i++) {
            if (a->type != mqi_integer)
                continue;

            if (!strcmp(a->name, "source_id")) {
                a->value.integer = source;
                srcidx = i;
            }
            else if (!strcmp(a->name, "sink_id")) {
                a->value.integer = sink;
                sinkidx = i;
            }
            else if (!strcmp(a->name, "connid")) {
                a->value.integer = conn;
                connidx = i;
            }
        }
    }

    status = mrp_resource_set_write_attributes(rset, "audio_playback", attrs);

    if (status < 0)
        mrp_log_error("Failed to update resource set attributes.");
    else
        mrp_log_info("Resource set attributes updated.");

    return status;
}


static void resctl_acquire(gamctl_t *gam, mrp_resource_set_t *set)
{
    mrp_log_info("Acquiring Genivi Audio Manager resource set.");
    mrp_resource_set_acquire(set, gam->seq++);
}


static int resctl_destroy(gamctl_t *gam, uint16_t connid)
{
    mrp_resource_set_t *rset;

    rset = mrp_htbl_remove(gam->rstbl, (void *)(ptrdiff_t)connid, FALSE);

    if (rset != NULL) {
        mrp_resource_set_destroy(rset);
        return TRUE;
    }
    else
        return FALSE;
}


static void resctl_recalc(gamctl_t *gam, int zoneid)
{
    uint32_t z;

    MRP_UNUSED(gam);

    mrp_log_info("Recalculating resource set allocations.");

    if (zoneid >= 0)
        mrp_resource_owner_recalc(zoneid);
    else
        for (z = 0; z < mrp_zone_count(); z++)
            mrp_resource_owner_recalc(z);
}


static void recalc_cb(mrp_deferred_t *d, void *user_data)
{
    gamctl_t *gam = (gamctl_t *)user_data;

    MRP_UNUSED(d);

    mrp_disable_deferred(gam->recalc);

    resctl_recalc(gam, -1);
}


static void resctl_schedule_recalc(gamctl_t *gam)
{
    mrp_log_info("Scheduling resource recalculation.");

    if (gam->recalc == NULL)
        gam->recalc = mrp_add_deferred(gam->self->ctx->ml, recalc_cb, gam);
    else
        mrp_enable_deferred(gam->recalc);
}


static void resctl_exit(gamctl_t *gam)
{
    if (gam != NULL) {
        if (gam->rsc != NULL) {
            mrp_log_info("Destroying Genivi Audio Manager resource client.");
            mrp_resource_client_destroy(gam->rsc);
            gam->rsc = NULL;
        }

        mrp_htbl_destroy(gam->rstbl, FALSE);
        gam->rstbl = NULL;
    }
}


static int route_init(gamctl_t *gam)
{
    route_t *r;
    path_t  *p;
    size_t   nroute, i;

    nroute      = MRP_ARRAY_SIZE(paths);
    gam->routes = mrp_allocz_array(route_t, nroute + 1);

    if (gam->routes == NULL)
        return FALSE;

    r = gam->routes;
    p = paths;
    for (i = 0; i < nroute; i++) {
        r->nhop = p->nhop;
        r->hops = mrp_allocz_array(uint16_t, r->nhop + 1);

        if (r->hops == NULL && r->nhop != 0)
            return FALSE;

        r->source = p->hops[0];
        r->sink   = p->hops[p->nhop - 1];

        mrp_log_info("Added a routing table entry for '%s' -> '%s'.",
                     r->source, r->sink);

        r++;
        p++;
    }

    gam->sourcef = 0;
    gam->sinkf   = 0;

    return TRUE;
}


static void route_exit(gamctl_t *gam)
{
    route_t *r;

    if (gam->routes == NULL)
        return;

    for (r = gam->routes; r->source != NULL; r++) {
        mrp_free(r->hops);
        r->hops = NULL;
        r->nhop = 0;
    }
}


static char *route_dump(gamctl_t *gam, char *buf, size_t size,
                        route_t *r, int verbose)
{
    path_t     *p;
    const char *t;
    char       *b;
    int         n, i;
    size_t      h, l;

    i = r - gam->routes;
    p = paths + i;

    t = "";
    b = buf;
    l = size;

    for (h = 0; h < r->nhop; h++) {
        if (verbose)
            n = snprintf(b, l, "%s%u(%s)", t, r->hops[h], p->hops[h]);
        else
            n = snprintf(b, l, "%s%u", t, r->hops[h]);

        if (n >= (int)l)
            return "dump_route: insufficient buffer";

        b += n;
        l -= n;
        t  = " -> ";
    }

    return buf;
}


static int route_incomplete(route_t *r)
{
    size_t h;

    for (h = 0; h < r->nhop; h++)
        if (!r->hops[h])
            return TRUE;

    return FALSE;
}


static route_t *route_connection(gamctl_t *gam, uint16_t source, uint16_t sink)
{
    uint32_t  sourcef = gam->sourcef;
    uint32_t  sinkf   = gam->sinkf;
    char      route[1024];
    route_t  *r;

    if (!resolve_nodes("source", sources, SOURCETBL, &gam->sourcef) ||
        !resolve_nodes("sink"  , sinks  , SINKTBL  , &gam->sinkf))
        return NULL;

    if (sourcef != gam->sourcef || sinkf != gam->sinkf)
        if (!resolve_routes(gam))
            return NULL;

    for (r = gam->routes; r->source; r++) {
        if (r->hops[0] == source && r->hops[r->nhop - 1] == sink) {
            if (!route_incomplete(r)) {
                mrp_log_info("Chosen route for connection: %s",
                             route_dump(gam, route, sizeof(route), r, TRUE));
                return r;
            }
            else {
                mrp_log_error("Route %u -> %u is unresolved/incomplete.",
                              source, sink);
                return NULL;
            }
        }
    }

    return NULL;
}


static int domctl_init(gamctl_t *gam)
{
    mrp_context_t           *ctx           = gam->self->ctx;
    mrp_domain_method_def_t  gam_methods[] = {
        { "request_route"    , 32, gamctl_route_cb     , gam },
        { "notify_disconnect",  8, gamctl_disconnect_cb, gam },
    };
    size_t                   gam_nmethod   = MRP_ARRAY_SIZE(gam_methods);

    if (mrp_register_domain_methods(ctx, gam_methods, gam_nmethod)) {
        mrp_log_info("Registered Genivi Audio Manager domain methods.");
        return TRUE;
    }
    else {
        mrp_log_error("Failed to register Genivi Audio Manager domain methods.");
        return FALSE;
    }
}


static void domctl_exit(gamctl_t *gam)
{
    MRP_UNUSED(gam);
}


static int gamctl_route_cb(int narg, mrp_domctl_arg_t *args,
                           uint32_t *nout, mrp_domctl_arg_t *outs,
                           void *user_data)
{
    gamctl_t           *gam = (gamctl_t *)user_data;
    uint16_t            source, sink, conn, *path;
    uint32_t            rsetid;
    const char         *error;
    size_t              i;
    mrp_resource_set_t *rset;
    route_t            *r;

    if (narg < 4) {
        error = "too few route request arguments (need route, conn, "
            "rset and paths)";
        goto error;
    }

    if (args[0].type != MRP_DOMCTL_ARRAY(UINT16)) {
        error = "invalid route (arg #0), array of uint16_t expected";
        goto error;
    }

    if (args[0].size != 2) {
        error = "invalid route (arg #0), 2 endpoints expected";
        goto error;
    }

    if (args[1].type != MRP_DOMCTL_UINT16) {
        error = "invalid connection id (arg #1), uint16_t expected";
        goto error;
    }

    if (args[2].type != MRP_DOMCTL_UINT32) {
        error = "invalid resource set id (arg #2), uint32_t expected";
        goto error;
    }

    source = ((uint16_t *)args[0].arr)[0];
    sink   = ((uint16_t *)args[0].arr)[1];
    conn   = args[1].u16;
    rsetid = args[2].u32;

    mrp_log_info("Got routing request for connection #%u:%u -> %u "
                 "(rset 0x%x) with %d possible routes.", conn, source, sink,
                 rsetid, narg - 3);

    r = route_connection(gam, source, sink);

    if (r == NULL || r->nhop == 0) {
        error = "no route";
        goto error;
    }

    if (!rsetid) {
        if ((rset = resctl_create(gam, source, sink, conn, 0)) == NULL) {
            error = "failed to create resouce set";
            goto error;
        }

        resctl_acquire(gam, rset);
    }
    else {
        resctl_update(gam, rsetid, source, sink, conn);
        resctl_schedule_recalc(gam);
    }

    path = mrp_allocz(r->nhop * sizeof(path[0]));

    if (path == NULL) {
        error = NULL;
        goto error;
    }

    for (i = 0; i < r->nhop; i++)
        path[i] = r->hops[i];

    *nout = 1;
    outs[0].type = MRP_DOMCTL_ARRAY(UINT16);
    outs[0].arr  = path;
    outs[0].size = r->nhop;

    return 0;

 error:
    *nout = 1;
    outs[0].type = MRP_DOMCTL_STRING;
    outs[0].str  = mrp_strdup(error);

    return -1;
}


static int gamctl_disconnect_cb(int narg, mrp_domctl_arg_t *args,
                                uint32_t *nout, mrp_domctl_arg_t *outs,
                                void *user_data)
{
    gamctl_t   *gam = (gamctl_t *)user_data;
    const char *error;
    uint16_t    conn;

    if (narg != 1) {
        error = "too few disconnect notification arguments (need connid)";
        goto error;
    }

    if (args[0].type != MRP_DOMCTL_UINT16) {
        error = "invalid disconnect connid (arg #0), uint16_t expected";
        goto error;
    }

    conn = args[0].u16;

    mrp_log_info("Got disconnect request for connection #%u.", conn);

    resctl_destroy(gam, conn);
    return TRUE;

 error:
    *nout = 1;
    outs[0].type = MRP_DOMCTL_STRING;
    outs[0].str  = mrp_strdup(error);

    return FALSE;
}


static int gamctl_init(mrp_plugin_t *plugin)
{
    gamctl_t *gam;

    gam = mrp_allocz(sizeof(*gam));

    if (gam == NULL)
        goto fail;

    gam->self    = plugin;
    plugin->data = gam;

    if (!resctl_init(gam))
        goto fail;

    if (!domctl_init(gam))
        goto fail;

    if (!route_init(gam))
        goto fail;

    return TRUE;

 fail:
    gamctl_exit(plugin);
    return FALSE;
}


static void gamctl_exit(mrp_plugin_t *plugin)
{
    gamctl_t *gam = plugin->data;

    resctl_exit(gam);
    domctl_exit(gam);
    route_exit(gam);
}


/* resource manager */

typedef struct dependency_s   dependency_t;


struct dependency_s {
    const char *db_table_name;
    mrp_resmgr_dependency_cb_t callback;
    bool changed;
};

struct mrp_resmgr_s {
    mrp_plugin_t *plugin;

    mrp_resmgr_config_t *config;
    mrp_event_watch_t *w;
    mrp_resmgr_backend_t *backend;
    mrp_resmgr_sources_t *sources;
    mrp_resmgr_sinks_t *sinks;
    mrp_resmgr_usecase_t *usecase;

    size_t ndepend;
    dependency_t *depends;
};

static int resource_update_cb(mrp_scriptlet_t *, mrp_context_tbl_t *);
static void add_depenedencies_to_resolver(mrp_resmgr_t *);

static void print_resources_cb(mrp_console_t *, void *, int, char **);
static void print_usecase_cb(mrp_console_t *, void *, int, char **);

static mrp_resmgr_config_t *config_create(mrp_plugin_t *);
static void config_destroy(mrp_resmgr_config_t *);

static void event_cb(mrp_event_watch_t *, int, mrp_msg_t *, void *);
static int subscribe_events(mrp_resmgr_t *);
static void unsubscribe_events(mrp_resmgr_t *);

MRP_CONSOLE_GROUP(manager_group, "gam", NULL, NULL, {
        MRP_TOKENIZED_CMD("resources", print_resources_cb, FALSE,
                          "resources", "prints managed resources",
                          "prints  the resources managed by "
                          "gam-resource-manager."),
        MRP_TOKENIZED_CMD("usecase", print_usecase_cb, FALSE,
                          "usecase", "prints the usecase values",
                          "prints  the usecase values that are used "
                          "for making routing/playback right decisions "
                          "by gam-resource-manager."),
});

static mrp_resmgr_t *resmgr_data;

mrp_resmgr_config_t *mrp_resmgr_get_config(mrp_resmgr_t *resmgr)
{
    return resmgr->config;
}

mrp_resmgr_backend_t *mrp_resmgr_get_backend(mrp_resmgr_t *resmgr)
{
    return resmgr->backend;
}


mrp_resmgr_sources_t *mrp_resmgr_get_sources(mrp_resmgr_t *resmgr)
{
    return resmgr->sources;
}

mrp_resmgr_sinks_t *mrp_resmgr_get_sinks(mrp_resmgr_t *resmgr)
{
    return resmgr->sinks;
}

mrp_resmgr_usecase_t *mrp_resmgr_get_usecase(mrp_resmgr_t *resmgr)
{
    return resmgr->usecase;
}



void mrp_resmgr_register_dependency(mrp_resmgr_t *resmgr,
                                    const char *db_table_name,
                                    mrp_resmgr_dependency_cb_t callback)
{
    size_t size;
    char dependency[512];
    int idx;

    MRP_ASSERT(resmgr && db_table_name, "invalid argument");

    snprintf(dependency, sizeof(dependency), "$%s", db_table_name);

    idx = resmgr->ndepend++;
    size = resmgr->ndepend * sizeof(dependency_t);

    if (!(resmgr->depends = mrp_realloc(resmgr->depends, size)) ||
        !(resmgr->depends[idx].db_table_name = mrp_strdup(dependency)))
    {
        mrp_log_error("gam-resource-manager: failed to allocate memory "
                      "for resource dependencies");
        resmgr->ndepend = 0;
        resmgr->depends = NULL;
        return;
    }

    resmgr->depends[idx].callback = callback;
}



static int resource_update_cb(mrp_scriptlet_t *script, mrp_context_tbl_t *ctbl)
{
    mrp_resmgr_t *resmgr = (mrp_resmgr_t *)script->data;
    uint32_t zoneid;
    size_t i;
    bool recalc;

    MRP_UNUSED(ctbl);

    MRP_ASSERT(resmgr, "invalid argument");

    printf("### %s() called\n", __FUNCTION__);

    for (recalc = false, i = 0;    i < resmgr->ndepend;    i++)
        recalc |= resmgr->depends[i].callback(resmgr);

    if (recalc) {
        printf("=> recalc resource allocations!\n");

        for (zoneid = 0;  zoneid < mrp_zone_count();  zoneid++)
            mrp_resource_owner_recalc(zoneid);
    }

    return TRUE;
}

static void add_depenedencies_to_resolver(mrp_resmgr_t *resmgr)
{
    static const char *target = "_gam_resources";
    static mrp_interpreter_t resource_updater = {
        { NULL, NULL },
        "gam_resource_updater",
        NULL,
        NULL,
        NULL,
        resource_update_cb,
        NULL
    };

    mrp_plugin_t *plugin;
    mrp_context_t *ctx;
    mrp_resolver_t *resolver;
    char buf[2048];
    const char *deps[256];
    size_t i,ndep;
    char *p, *e;
    int ok;

    MRP_ASSERT(resmgr, "invalid argument");

    plugin = resmgr->plugin;

    if (!(ctx = plugin->ctx) || !(resolver = ctx->r))
        return;

    if (!(ndep = resmgr->ndepend) || !resmgr->depends)
        return;

    MRP_ASSERT(ndep < MRP_ARRAY_SIZE(deps), "dependency overflow");

    for (e = (p = buf) + sizeof(buf), i = 0;     i < ndep;     i++) {
        deps[i] = resmgr->depends[i].db_table_name;

        if (p < e)
            p += snprintf(p, e-p, " %s", resmgr->depends[i].db_table_name);
    }

    printf("%s:%s\n\tresource_recalc()\n\n", target, buf);

    ok = mrp_resolver_add_prepared_target(resolver, target, deps, ndep,
                                          &resource_updater, NULL, resmgr);
    if (!ok) {
        mrp_log_error("gam-resource-manager: failed to install "
                      "resolver target '%s'", target);
    }
}


static void print_resources_cb(mrp_console_t *c, void *user_data,
                               int argc, char **argv)
{
    MRP_UNUSED(c);
    MRP_UNUSED(user_data);
    MRP_UNUSED(argc);
    MRP_UNUSED(argv);

    printf("Resources managed by gam-resource-manager:\n");

    printf("\n");
}

static void print_usecase_cb(mrp_console_t *c, void *user_data,
                             int argc, char **argv)
{
    mrp_resmgr_t *resmgr;
    char buf[16384];

    MRP_UNUSED(c);
    MRP_UNUSED(user_data);
    MRP_UNUSED(argc);
    MRP_UNUSED(argv);

    if ((resmgr = resmgr_data)) {
        mrp_usecase_print(resmgr->usecase, buf, sizeof(buf));
        printf("Current usecase:\n%s\n", buf);
    }
}


static mrp_resmgr_config_t *config_create(mrp_plugin_t *plugin)
{
    mrp_plugin_arg_t    *args = plugin->args;
    mrp_resmgr_config_t *config;

    if ((config = mrp_allocz(sizeof(mrp_resmgr_config_t)))) {
        config->confdir    = mrp_strdup(args[ARG_CONFDIR].str);
        config->prefix     = mrp_strdup(args[ARG_PREFIX].str);
        config->confnams   = mrp_strdup(args[ARG_DECISION_NAMES].str);
        config->max_active = args[ARG_MAX_ACTIVE].i32;
    }

    return config;
}

static void config_destroy(mrp_resmgr_config_t *config)
{
    if (config) {
        mrp_free((void *)config->confdir);
        mrp_free((void *)config->prefix);
        mrp_free(config);
    }
}


static void event_cb(mrp_event_watch_t *w, int id, mrp_msg_t *event_data,
                     void *user_data)
{
    mrp_plugin_t      *plugin   = (mrp_plugin_t *)user_data;
#if 0
    mrp_plugin_arg_t  *args     = plugin->args;
#endif
    mrp_resmgr_t      *resmgr   = (mrp_resmgr_t *)plugin->data;
    const char        *event    = mrp_get_event_name(id);
    uint16_t           tag_inst = MRP_PLUGIN_TAG_INSTANCE;
    uint16_t           tag_name = MRP_PLUGIN_TAG_PLUGIN;
    const char        *inst;
    const char        *name;
    int                success;

    MRP_UNUSED(w);

    mrp_log_info("%s: got event 0x%x (%s):", plugin->instance, id, event);

    if (resmgr && event) {
        if (!strcmp(event, MRP_PLUGIN_EVENT_STARTED)) {
            success = mrp_msg_get(event_data,
                                  MRP_MSG_TAG_STRING(tag_inst, &inst),
                                  MRP_MSG_TAG_STRING(tag_name, &name),
                                  MRP_MSG_END);
            if (success) {
                if (!strcmp(inst, plugin->instance)) {
                    /* initialize here */
                }
            }
        } /* if PLUGIN_STARTED */
    }
}

static int subscribe_events(mrp_resmgr_t *resmgr)
{
    mrp_plugin_t *plugin = resmgr->plugin;
    mrp_event_mask_t events;

    mrp_set_named_events(&events,
                         MRP_PLUGIN_EVENT_LOADED,
                         MRP_PLUGIN_EVENT_STARTED,
                         MRP_PLUGIN_EVENT_FAILED,
                         MRP_PLUGIN_EVENT_STOPPING,
                         MRP_PLUGIN_EVENT_STOPPED,
                         MRP_PLUGIN_EVENT_UNLOADED,
                         NULL);

    resmgr->w = mrp_add_event_watch(&events, event_cb, plugin);

    return (resmgr->w != NULL);
}


static void unsubscribe_events(mrp_resmgr_t *resmgr)
{
    if (resmgr->w) {
        mrp_del_event_watch(resmgr->w);
        resmgr->w = NULL;
    }
}


static int manager_init(mrp_plugin_t *plugin)
{
    mrp_resmgr_t *resmgr;

    mrp_log_info("%s() called for GAM resource manager instance '%s'...",
                 __FUNCTION__, plugin->instance);


    if (!(resmgr = mrp_allocz(sizeof(*resmgr)))) {
        mrp_log_error("Failed to allocate private data for GAM resource "
                      "manager plugin instance %s.", plugin->instance);
        return FALSE;
    }

    resmgr->plugin = plugin;
    resmgr->config = config_create(plugin);
    resmgr->backend = mrp_resmgr_backend_create(resmgr);
    resmgr->sources = mrp_resmgr_sources_create(resmgr);
    resmgr->sinks = mrp_resmgr_sinks_create(resmgr);
    resmgr->usecase = mrp_resmgr_usecase_create(resmgr);

    plugin->data = resmgr;
    resmgr_data = resmgr;

    subscribe_events(resmgr);
    mqi_open();
    add_depenedencies_to_resolver(resmgr);

    /*******************************/
    mrp_resmgr_sink_add(resmgr, "speakers"        , 0, NULL);
    mrp_resmgr_sink_add(resmgr, "wiredHeadset"    , 0, NULL);
    mrp_resmgr_sink_add(resmgr, "usbHeadset"      , 0, NULL);
    mrp_resmgr_sink_add(resmgr, "btHeadset"       , 0, NULL);
    mrp_resmgr_sink_add(resmgr, "voiceRecognition", 0, NULL);

    mrp_resmgr_source_add(resmgr, "wrtApplication", 0);
    mrp_resmgr_source_add(resmgr, "icoApplication", 0);
    mrp_resmgr_source_add(resmgr, "phone"         , 0);
    mrp_resmgr_source_add(resmgr, "radio"         , 0);
    mrp_resmgr_source_add(resmgr, "microphone"    , 0);
    mrp_resmgr_source_add(resmgr, "navigator"     , 0);
    /*******************************/

    return TRUE;
}


static void manager_exit(mrp_plugin_t *plugin)
{
    mrp_resmgr_t *resmgr;
    size_t i;

    mrp_log_info("%s() called for GAM resource manager instance '%s'...",
                 __FUNCTION__, plugin->instance);

    if ((resmgr = plugin->data) && resmgr_data == resmgr) {
        unsubscribe_events(resmgr);

        for (i = 0;  i < resmgr->ndepend;  i++)
            mrp_free((void *)resmgr->depends[i].db_table_name);
        mrp_free((void *)resmgr->depends);

        mrp_resmgr_backend_destroy(resmgr->backend);
        mrp_resmgr_sources_destroy(resmgr->sources);
        mrp_resmgr_sinks_destroy(resmgr->sinks);
        mrp_resmgr_usecase_destroy(resmgr->usecase);
        config_destroy(resmgr->config);

        mrp_free(resmgr);

        resmgr_data = NULL;
    }
}


static int gam_init(mrp_plugin_t *plugin)
{
    return manager_init(plugin) &&
            gamctl_init(plugin) &&
            gam_connect_init(plugin);
}

static void gam_exit(mrp_plugin_t *plugin)
{
    gam_connect_exit(plugin);
    gamctl_exit(plugin);
    manager_exit(plugin);
}

#define GAM_DESCRIPTION "Plugin to implement GAM support"
#define GAM_HELP        "Maybe later ..."
#define GAM_VERSION      MRP_VERSION_INT(0, 0, 1)
#define GAM_AUTHORS     "Murphy Team <murphy-dev@lists.01.org>"

static mrp_plugin_arg_t arg_defs[] = {
    MRP_PLUGIN_ARGIDX(ARG_ZONE, STRING, "zone", DEFAULT_ZONE),
    MRP_PLUGIN_ARGIDX(ARG_DEFAULT_SINK, STRING, "default_sink", DEFAULT_SINK),
    MRP_PLUGIN_ARGIDX(ARG_APP_DEFAULT, STRING, "app_default", DEFAULT_APP_DEFAULT),
    MRP_PLUGIN_ARGIDX(ARG_APP_MAPPING, OBJECT, "app_mapping", DEFAULT_APP_MAPPING),
    MRP_PLUGIN_ARGIDX(ARG_PLAYBACK_RESOURCE, STRING, "playback_resource", DEFAULT_PLAYBACK_RESOURCE),
    MRP_PLUGIN_ARGIDX(ARG_RECORDING_RESOURCE, STRING, "recording_resource", DEFAULT_RECORDING_RESOURCE),
    MRP_PLUGIN_ARGIDX(ARG_MURPHY_CONNECTS, BOOL, "murphy_connects", TRUE),
    MRP_PLUGIN_ARGIDX(ARG_CONFDIR, STRING, "config_dir", MRP_RESMGR_DEFAULT_CONFDIR),
    MRP_PLUGIN_ARGIDX(ARG_PREFIX, STRING, "prefix", MRP_RESMGR_DEFAULT_PREFIX),
    MRP_PLUGIN_ARGIDX(ARG_DECISION_NAMES, STRING, "decision_names", MRP_RESMGR_DEFAULT_NAMES),
    MRP_PLUGIN_ARGIDX(ARG_MAX_ACTIVE, INT32, "max_active", 1),
};

#undef INTEGER_ARG
#undef STRING_ARG

MURPHY_REGISTER_PLUGIN("gam-resource-manager",
                       GAM_VERSION,
                       GAM_DESCRIPTION,
                       GAM_AUTHORS,
                       GAM_HELP,
                       MRP_SINGLETON,
                       gam_init,
                       gam_exit,
                       arg_defs, MRP_ARRAY_SIZE(arg_defs),
                       NULL, 0,
                       NULL, 0,
                       &manager_group);

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
