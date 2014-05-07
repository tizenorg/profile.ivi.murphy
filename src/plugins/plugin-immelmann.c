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

#include <murphy/resource/client-api.h>
#include <murphy/resource/manager-api.h>

#include <murphy/resource/resource-set.h>
#include <murphy/resource/zone.h>
#include <murphy/resource/application-class.h>

#define PRIORITY_CLASS MRP_LUA_CLASS_SIMPLE(routing_sink_priority)


/* Listen for new resource sets. When one appears, check if it already has
 * attributes "sink" and "connection_id" set. If it has, the request has come
 * from a GAM-aware application, otherwise not. If not, tell GAM that a new
 * application has come around to ask for routing. */


typedef struct {
    char *name;
    mrp_list_hook_t sinks;
} priority_t;

typedef struct {
    mrp_mainloop_t *ml;

    char *zone;
    char *default_sink;

    mrp_htbl_t *pqs; /* "application_class" -> priority_t */

    mrp_event_watch_t *w;
    int events[4];
} immelmann_t;

typedef struct {
    char *name;
    mrp_list_hook_t hook;
} routing_target_t;


/* global context for Lua configuration */

static immelmann_t *global_ctx;

enum {
    ARG_ZONE,
    ARG_DEFAULT_SINK,
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
    routing_sink_priority,          /* main class & constructor name */
    priority_t,        /* userdata type */
    priority_destroy,               /* userdata destructor */
    MRP_LUA_METHOD_LIST (           /* main class methods */
        MRP_LUA_METHOD_CONSTRUCTOR  (priority_create)
    ),
    MRP_LUA_METHOD_LIST (           /* main class overrides */
        MRP_LUA_OVERRIDE_CALL       (priority_create)
        MRP_LUA_OVERRIDE_GETFIELD   (priority_getfield)
        MRP_LUA_OVERRIDE_SETFIELD   (priority_setfield)
    )
);

void set_connection_id(immelmann_t *ctx, mrp_resource_set_t *rset,
        const char *resource, uint32_t conn_id)
{
    mrp_attr_t *conn_id_attr;
    mrp_zone_t *zone;

    MRP_UNUSED(resource);

    conn_id_attr = mrp_resource_set_get_attribute_by_name(rset,
            "audio_playback", "conn_id");

    if (conn_id_attr && conn_id_attr->type == mqi_unsignd) {
        conn_id_attr->value.unsignd = conn_id;
    }

    /* GAM domains map in some sense to Murphy zones, so we'll play only
     * in some default GAM zone for the recalculation. */

    zone = mrp_zone_find_by_name(ctx->zone);

    if (zone)
        mrp_resource_owner_recalc(zone->id);
}

bool is_sink_available(immelmann_t *ctx, const char *sink)
{
    /* TODO: check from the database table if GAM has provided this sink */

    MRP_UNUSED(ctx);
    MRP_UNUSED(sink);

    return TRUE;
}

void create_priority_class(immelmann_t *ctx)
{
    lua_State *L = mrp_lua_get_lua_state();

    MRP_UNUSED(ctx);

    mrp_lua_create_object_class(L, PRIORITY_CLASS);
}

mrp_list_hook_t *get_priorities_for_application_class(immelmann_t *ctx,
        const char *name)
{
    priority_t *prio = mrp_htbl_lookup(ctx->pqs, (char *) name);

    if (prio) {
        return &prio->sinks;
    }

    return NULL;
}

const char *get_default_sink(immelmann_t *ctx, mrp_resource_set_t *rset)
{
    mrp_list_hook_t *pq = NULL;
    mrp_list_hook_t *p, *n;

    /* get the priority list from Murphy configuration for this particular
     * application class */

    pq = get_priorities_for_application_class(ctx, rset->class.ptr->name);

    if (!pq)
        return NULL;

    /* go through the list while checking if the defined sinks are available */

    mrp_list_foreach(pq, p, n) {
        routing_target_t *t = mrp_list_entry(p, typeof(*t), hook);

        if (is_sink_available(ctx, t->name)) {
            return t->name;
        }
    }

    return NULL;
}

void register_with_gam(immelmann_t *ctx, mrp_resource_set_t *rset,
        const char *source)
{
    const char *sink;

    /* ask GAM for the connection via control interface */

    sink = get_default_sink(ctx, rset);

    if (!sink) {
        mrp_log_error("Error finding default sink, using global default");
        sink = ctx->default_sink;
    }

    mrp_log_info("register rset %u with GAM! (%s -> %s)",
            rset->id, source, sink);

    /* TODO: call the function here:
     * register_source(sink, source, rset->id, callback); */

    return;
}

void resource_set_event(mrp_event_watch_t *w, int id, mrp_msg_t *event_data,
        void *user_data)
{
    immelmann_t *ctx = (immelmann_t *) user_data;
    uint32_t rset_id;
    uint16_t tag = MRP_RESOURCE_TAG_RSET_ID;

    MRP_UNUSED(w);

    mrp_msg_get(event_data, MRP_MSG_TAG_UINT32(tag, &rset_id), NULL);

    mrp_log_info("resource set event '%d' for '%u' received!", id, rset_id);

    if (id == ctx->events[CREATED]) {
        mrp_resource_set_t *rset = mrp_resource_set_find_by_id(rset_id);

        mrp_log_info("Resource set %u (%p) was created", rset_id, rset);
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

            if (strcmp(name, "audio_playback") == 0)
                audio_playback = resource;

            else if (strcmp(name, "audio_recording") == 0)
                audio_recording = resource;

            if (audio_playback && audio_recording)
                break;
        }

        if (audio_playback) {
            mrp_attr_t *source; /* the application itself */
            mrp_attr_t *conn_id;

            source = mrp_resource_set_get_attribute_by_name(rset,
                    "audio_playback", "source");
            conn_id = mrp_resource_set_get_attribute_by_name(rset,
                    "audio_playback", "conn_id");

            if (!source || !conn_id) {
                /* what is this? */
                mrp_log_error("source or conn_id attributes not defined!");
            }
            else if (conn_id->type != mqi_unsignd ||
                    source->type != mqi_string) {
                mrp_log_error("source or conn_id types don't match!");
            }
            else if (conn_id->value.unsignd == 0) {
                /* this connection is not already managed by GAM */

                register_with_gam(ctx, rset, source->value.string);
            }
        }

#if 0
        /* TODO: think properly about audio recording */

        if (audio_recording) {
            mrp_attr_t *sink; /* the application itself */
            mrp_attr_t *conn_id;

            sink = mrp_resource_set_get_attribute_by_name(rset,
                    "audio_recording", "sink");
            conn_id = mrp_resource_set_get_attribute_by_name(rset,
                    "audio_recording", "conn_id");

            if (!sink || !conn_id) {
                /* what is this? */
                mrp_log_error("sink or conn_id attributes not defined!");
            }
            else if (conn_id->type == mqi_unsignd &&
                    conn_id->value.unsignd == 0) {
                /* this connection is not already managed by GAM */
                need_to_register = TRUE;
            }
        }
#endif

        mrp_log_info("Resource set %u (%p) was acquired", rset_id, rset);
    }
    else if (id == ctx->events[RELEASE]) {
        mrp_resource_set_t *rset = mrp_resource_set_find_by_id(rset_id);

        mrp_log_info("Resource set %u (%p) was released", rset_id, rset);
    }
    else if (id == ctx->events[DESTROYED]) {
        mrp_log_info("Resource set %u was destroyed", rset_id);
    }
}

static int priority_create(lua_State *L)
{
    priority_t *prio;
    size_t fldnamlen;
    const char *fldnam;
    char *name = NULL;
    mrp_list_hook_t priority_queue;
    immelmann_t *ctx = global_ctx;

    MRP_LUA_ENTER;

    mrp_log_error(">>> priority_create");

    mrp_list_init(&priority_queue);

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

            mrp_log_error("immelmann application class name: %s", name);
        }
        else if (strncmp(fldnam, "priority_queue", fldnamlen) == 0) {

            if (lua_istable(L, -1)) {

                /* push NIL to stack as the first key */
                lua_pushnil(L);

                while (lua_next(L, -2)) {
                    const char *sink_name;
                    mrp_log_error(">>> priority_create: processing table");

                    /* only string values are accepted */
                    if (lua_isstring(L, -1)) {
                        routing_target_t *sink;

                        mrp_log_error(">>> priority_create: isstring match");
                        sink_name = lua_tostring(L, -1);

                        sink = mrp_allocz(sizeof(routing_target_t));

                        sink->name = mrp_strdup(sink_name);
                        mrp_list_init(&sink->hook);

                        mrp_list_append(&priority_queue, &sink->hook);

                        mrp_log_info("immelmann sink name: %s", sink->name);
                    }

                    /* remove the value, keep key */
                    lua_pop(L, 1);
                }
            }
        }
        else {
            mrp_log_error("unknown field");
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
        mrp_log_info("application class priority object '%s' created", name);
    }

    prio->sinks = priority_queue;

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

    mrp_list_foreach(&prio->sinks, p, n) {
        routing_target_t *t = mrp_list_entry(p, typeof(*t), hook);

        mrp_list_delete(&t->hook);
        mrp_free(t->name);
        mrp_free(t);
    }

    prio->name = NULL;
    mrp_list_init(&prio->sinks);

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

    if (!ctx->zone || !ctx->default_sink)
        goto error;

    ctx->ml = plugin->ctx->ml;
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

    mrp_free(ctx->zone);
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

    mrp_free(ctx->zone);
    mrp_free(ctx);

    global_ctx = NULL;
}

#define DEFAULT_ZONE    "driver"
#define DEFAULT_SINK    "speakers"

#define PLUGIN_DESCRIPTION "Plugin to add PA streams to GAM (with resource support)"
#define PLUGIN_HELP        "Help coming later."
#define PLUGIN_AUTHORS     "Ismo Puustinen <ismo.puustinen@intel.com>"
#define PLUGIN_VERSION     MRP_VERSION_INT(0, 0, 1)

static mrp_plugin_arg_t plugin_args[] = {
    MRP_PLUGIN_ARGIDX(ARG_ZONE, STRING, "zone", DEFAULT_ZONE),
    MRP_PLUGIN_ARGIDX(ARG_DEFAULT_SINK, STRING, "default_sink", DEFAULT_SINK),
};

MURPHY_REGISTER_PLUGIN("immelmann",
                       PLUGIN_VERSION, PLUGIN_DESCRIPTION, PLUGIN_AUTHORS,
                       PLUGIN_HELP, MRP_SINGLETON, plugin_init, plugin_exit,
                       plugin_args, MRP_ARRAY_SIZE(plugin_args),
                       NULL, 0,
                       NULL, 0,
                       NULL);
