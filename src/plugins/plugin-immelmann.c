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

#include <glib.h>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include <murphy/common.h>

#include <murphy/core/plugin.h>
#include <murphy/core/event.h>

#include <murphy/resource/resource-set.h>
#include <murphy/resource/client-api.h>

/* Listen for new resource sets. When one appears, check if it already has
 * attributes "sink" and "connection_id" set. If it has, the request has come
 * from a GAM-aware application, otherwise not. If not, tell GAM that a new
 * application has come around to ask for routing. */

typedef struct {
    mrp_mainloop_t *ml;

    mrp_event_watch_t *w;
    int events[4];
} immelmann_t;

enum {
    CREATED = 0,
    ACQUIRE,
    RELEASE,
    DESTROYED,
};

const char *get_default_sink(immelmann_t *ctx, mrp_resource_set_t *rset)
{
    /* TODO: placeholder for the real algorithm for finding out the default
     * route for a given application class */
    return "speakers";
}

void register_with_gam(immelmann_t *ctx, mrp_resource_set_t *rset,
        const char *source)
{
    const char *sink;

    /* ask GAM for the connection via control interface */

    sink = get_default_sink(ctx, rset);

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
        bool need_to_register = FALSE;

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

#if 0
        if (need_to_register) {
            register_with_gam(ctx, rset, !!audio_playback, !!audio_recording);
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

static int plugin_init(mrp_plugin_t *plugin)
{
    immelmann_t *ctx;
    ctx = mrp_allocz(sizeof(immelmann_t));
    mrp_event_mask_t mask = 0;

    mrp_reset_event_mask(&mask);

    if (!ctx)
        goto error;

    ctx->ml = plugin->ctx->ml;
    plugin->data = ctx;

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

    ctx->w = mrp_add_event_watch(&mask, resource_set_event, ctx);

    mrp_log_info("Launched the Murphy Immelmann plugin!");
    return TRUE;

error:
    mrp_log_error("Error starting Murphy Immelmann plugin!");
    return FALSE;
}

static void plugin_exit(mrp_plugin_t *plugin)
{
    immelmann_t *ctx = (immelmann_t *) plugin->data;

    if (ctx->w)
        mrp_del_event_watch(ctx->w);

    mrp_free(ctx);
}

#if 0
enum {
    ARG_ADDRESS,
};

#define DEFAULT_ADDRESS    "foo"
#endif

#define PLUGIN_DESCRIPTION "Plugin to add PA streams to GAM (with resource support)"
#define PLUGIN_HELP        "Help coming later."
#define PLUGIN_AUTHORS     "Ismo Puustinen <ismo.puustinen@intel.com>"
#define PLUGIN_VERSION     MRP_VERSION_INT(0, 0, 1)

#if 0
static mrp_plugin_arg_t plugin_args[] = {
    MRP_PLUGIN_ARGIDX(ARG_ADDRESS, STRING, "address", DEFAULT_ADDRESS),
};
#else
static mrp_plugin_arg_t plugin_args[] = {
};
#endif

MURPHY_REGISTER_PLUGIN("immelmann",
                       PLUGIN_VERSION, PLUGIN_DESCRIPTION, PLUGIN_AUTHORS,
                       PLUGIN_HELP, MRP_SINGLETON, plugin_init, plugin_exit,
                       plugin_args, MRP_ARRAY_SIZE(plugin_args),
                       NULL, 0,
                       NULL, 0,
                       NULL);
