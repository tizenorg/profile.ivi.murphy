/*
 * Copyright (c) 2012, Intel Corporation
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
#include <murphy/common/debug.h>
#include <murphy/core/plugin.h>
#include <murphy/core/console.h>
#include <murphy/core/event.h>
#include <murphy/core/context.h>
#include <murphy/core/lua-bindings/murphy.h>

#include <murphy-db/mql.h>
#include <murphy-db/mqi.h>

#include <murphy/resource/config-api.h>
#include <murphy/resource/manager-api.h>
#include <murphy/resource/client-api.h>
#include <murphy/resource/protocol.h>

#include "screen.h"
#include "audio.h"
#include "appid.h"

struct mrp_resmgr_data_s {
    mrp_plugin_t        *plugin;
    mrp_event_watch_t   *w;
    mrp_resmgr_screen_t *screen;
    mrp_resmgr_audio_t  *audio;
    mrp_resmgr_appid_t  *appid;
    mrp_htbl_t          *resources;
    int                  ndepend;
    const char         **depends;
    mrp_zone_mask_t      zones;
};

static void print_resources_cb(mrp_console_t *, void *, int, char **);

MRP_CONSOLE_GROUP(manager_group, "ivi-resource-manager", NULL, NULL, {
        MRP_TOKENIZED_CMD("resources", print_resources_cb, FALSE,
                          "resources", "prints managed resources",
                          "prints  the resources managed by "
                          "ivi-resource-manager."),
});

static mrp_resmgr_data_t *resmgr_data;

void mrp_resmgr_register_dependency(mrp_resmgr_data_t *data,
                                    const char *db_table_name)
{
    size_t size;
    const char **depends;
    char dependency[512];
    int idx;

    MRP_ASSERT(data && db_table_name, "invalid argument");

    idx = data->ndepend;
    size = (idx + 1) * sizeof(const char *);

    if (!(depends = mrp_realloc(data->depends, size))) {
        mrp_log_error("ivi-resource-manager: failed to allocate memory "
                      "for resource dependencies");
        data->ndepend = 0;
        data->depends = NULL;
        return;
    }

    snprintf(dependency, sizeof(dependency), "$%s", db_table_name);

    if (!(depends[idx] = mrp_strdup(dependency))) {
        mrp_log_error("ivi-resource-manager: failed to strdup dependency");
        data->depends = depends;
        return;
    }

    data->ndepend = idx + 1;
    data->depends = depends;
}

void mrp_resmgr_insert_resource(mrp_resmgr_data_t *data,
                                mrp_zone_t *zone,
                                mrp_resource_t *key,
                                void *resource)
{
    uint32_t zoneid;

    MRP_ASSERT(data && zone && key && resource, "invalid argument");
    MRP_ASSERT(data->resources, "uninitialised data structure");

    zoneid = mrp_zone_get_id(zone);

    data->zones |= ((mrp_zone_mask_t)1 << zoneid);

    mrp_htbl_insert(data->resources, key, resource);
}

void *mrp_resmgr_remove_resource(mrp_resmgr_data_t *data,
                                 mrp_zone_t *zone,
                                 mrp_resource_t *key)
{
    MRP_ASSERT(data && zone && key, "invalid argument");
    MRP_ASSERT(data->resources, "uninitialised data structure");

    return mrp_htbl_remove(data->resources, key, FALSE);
}

void *mrp_resmgr_lookup_resource(mrp_resmgr_data_t *data, mrp_resource_t *key)
{
    MRP_ASSERT(data && key, "invalid argument");
    MRP_ASSERT(data->resources, "uninitialised data structure");

    return mrp_htbl_lookup(data->resources, key);
}

mrp_resmgr_screen_t *mrp_resmgr_get_screen(mrp_resmgr_data_t *data)
{
    MRP_ASSERT(data, "invalid argument");
    MRP_ASSERT(data->screen, "confused with data structures");

    return data->screen;
}

mrp_resmgr_audio_t *mrp_resmgr_get_audio(mrp_resmgr_data_t *data)
{
    MRP_ASSERT(data, "invalid argument");
    MRP_ASSERT(data->audio, "confused with data structures");

    return data->audio;
}

mrp_resmgr_appid_t *mrp_resmgr_get_appid(mrp_resmgr_data_t *data)
{
    MRP_ASSERT(data, "invalid argument");
    MRP_ASSERT(data->appid, "confused with data structures");

    return data->appid;
}

static void print_resources_cb(mrp_console_t *c, void *user_data,
                               int argc, char **argv)
{
    const char *zones[MRP_ZONE_MAX + 1];
    uint32_t zoneid;
    char buf[65536];

    MRP_UNUSED(c);
    MRP_UNUSED(user_data);
    MRP_UNUSED(argc);
    MRP_UNUSED(argv);

    mrp_zone_get_all_names(MRP_ZONE_MAX+1, zones);

    printf("Resources managed by ivi-resource-manager:\n");

    for (zoneid = 0;   zones[zoneid];  zoneid++) {
        printf("   Zone '%s':\n", zones[zoneid]);

        mrp_resmgr_screen_print(resmgr_data->screen, zoneid, buf, sizeof(buf));
        fputs(buf, stdout);

        mrp_resmgr_audio_print(resmgr_data->audio, zoneid, buf, sizeof(buf));
        fputs(buf, stdout);
    }

    printf("\n");
}


static int resource_update_cb(mrp_scriptlet_t *script, mrp_context_tbl_t *ctbl)
{
    mrp_resmgr_data_t *data = (mrp_resmgr_data_t *)script->data;
    mrp_zone_mask_t mask;
    uint32_t zoneid;

    MRP_UNUSED(ctbl);

    for (mask = data->zones, zoneid = 0;   mask;   mask >>= 1, zoneid++) {
        if ((mask & 1))
            mrp_resource_owner_recalc(zoneid);
    }

    return TRUE;
}

static void add_depenedencies_to_resolver(mrp_resmgr_data_t *data)
{
    static const char *target = "_ivi_resources";
    static mrp_interpreter_t resource_updater = {
        { NULL, NULL },
        "resource_updater",
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
    char *p, *e;
    int i;
    int success;

    MRP_ASSERT(data, "invalid argument");

    plugin = data->plugin;

    if (!(ctx = plugin->ctx) || !(resolver = ctx->r))
        return;

    if (!data->ndepend || !data->depends)
        return;

    for (i = 0, e = (p = buf) + sizeof(buf); i < data->ndepend && p < e; i++)
        p += snprintf(p, e-p, " %s", data->depends[i]);

    printf("%s:%s\n\tresource_recalc()\n\n", target, buf);

    success = mrp_resolver_add_prepared_target(resolver, target,
                                               data->depends, data->ndepend,
                                               &resource_updater, NULL, data);
    if (!success) {
        mrp_log_error("ivi-resource-manager: failed to install "
                      "resolver target '%s'", target);
    }
}

static void event_cb(mrp_event_watch_t *w, int id, mrp_msg_t *event_data,
                     void *user_data)
{
    mrp_plugin_t      *plugin   = (mrp_plugin_t *)user_data;
#if 0
    mrp_plugin_arg_t  *args     = plugin->args;
#endif
    mrp_resmgr_data_t *data     = (mrp_resmgr_data_t *)plugin->data;
    const char        *event    = mrp_get_event_name(id);
    uint16_t           tag_inst = MRP_PLUGIN_TAG_INSTANCE;
    uint16_t           tag_name = MRP_PLUGIN_TAG_PLUGIN;
    const char        *inst;
    const char        *name;
    int                success;

    MRP_UNUSED(w);

    mrp_log_info("%s: got event 0x%x (%s):", plugin->instance, id, event);

    if (data && event) {
        if (!strcmp(event, MRP_PLUGIN_EVENT_STARTED)) {
            success = mrp_msg_get(event_data,
                                  MRP_MSG_TAG_STRING(tag_inst, &inst),
                                  MRP_MSG_TAG_STRING(tag_name, &name),
                                  MRP_MSG_END);
            if (success) {
                if (!strcmp(inst, plugin->instance)) {
                    data->screen = mrp_resmgr_screen_create(data);
                    data->audio  = mrp_resmgr_audio_create(data);
                    data->appid  = mrp_resmgr_appid_create(data);

                    add_depenedencies_to_resolver(data);
                }
            }
        } /* if PLUGIN_STARTED */
    }
}


static int subscribe_events(mrp_plugin_t *plugin)
{
    mrp_resmgr_data_t *data = (mrp_resmgr_data_t *)plugin->data;
    mrp_event_mask_t   events;

    mrp_set_named_events(&events,
                         MRP_PLUGIN_EVENT_LOADED,
                         MRP_PLUGIN_EVENT_STARTED,
                         MRP_PLUGIN_EVENT_FAILED,
                         MRP_PLUGIN_EVENT_STOPPING,
                         MRP_PLUGIN_EVENT_STOPPED,
                         MRP_PLUGIN_EVENT_UNLOADED,
                         NULL);

    data->w = mrp_add_event_watch(&events, event_cb, plugin);

    return (data->w != NULL);
}


static void unsubscribe_events(mrp_plugin_t *plugin)
{
    mrp_resmgr_data_t *data = (mrp_resmgr_data_t *)plugin->data;

    if (data->w) {
        mrp_del_event_watch(data->w);
        data->w = NULL;
    }
}


static int hash_compare(const void *key1, const void *key2)
{
    if (key1 < key2)
        return -1;
    if (key1 > key2)
        return 1;
    return 0;
}

static uint32_t hash_function(const void *key)
{
    uint64_t k = (ptrdiff_t)key;

    return (uint32_t)((k >> 4) & 0xffffffff);
}


static int manager_init(mrp_plugin_t *plugin)
{
#if 0
    mrp_plugin_arg_t  *args = plugin->args;
#endif
    mrp_resmgr_data_t *data;
    mrp_htbl_config_t  cfg;

    mrp_log_info("%s() called for IVI resource manager instance '%s'...",
                 __FUNCTION__, plugin->instance);

    cfg.nentry = 256;
    cfg.comp = hash_compare;
    cfg.hash = hash_function;
    cfg.free = NULL;
    cfg.nbucket = cfg.nentry / 4;

    if (!(data = mrp_allocz(sizeof(*data)))) {
        mrp_log_error("Failed to allocate private data for IVI resource "
                      "manager plugin instance %s.", plugin->instance);
        return FALSE;
    }

    data->plugin = plugin;
    data->resources = mrp_htbl_create(&cfg);

    plugin->data = data;
    resmgr_data  = data;

    subscribe_events(plugin);

    return TRUE;
}


static void manager_exit(mrp_plugin_t *plugin)
{
    mrp_resmgr_data_t *data;

    mrp_log_info("%s() called for IVI resource manager instance '%s'...",
                 __FUNCTION__, plugin->instance);

    unsubscribe_events(plugin);

    if ((data = plugin->data) && data == resmgr_data) {
        mrp_resmgr_screen_destroy(data->screen);
        mrp_resmgr_audio_destroy(data->audio);
        mrp_resmgr_appid_destroy(data->appid);
    }
}


#define MANAGER_DESCRIPTION "Plugin to implement IVI resources"
#define MANAGER_HELP        "Maybe later ..."
#define MANAGER_VERSION      MRP_VERSION_INT(0, 0, 1)
#define MANAGER_AUTHORS     "Janos Kovacs <jankovac503@gmail.com>"

static mrp_plugin_arg_t args[] = {
};


MURPHY_REGISTER_PLUGIN("resource-manager",
                       MANAGER_VERSION,
                       MANAGER_DESCRIPTION,
                       MANAGER_AUTHORS,
                       MANAGER_HELP,
                       MRP_SINGLETON,
                       manager_init,
                       manager_exit,
                       args, MRP_ARRAY_SIZE(args),
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
