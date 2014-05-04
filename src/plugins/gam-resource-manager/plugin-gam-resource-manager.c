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
#include <murphy/common/debug.h>
#include <murphy/core/plugin.h>
#include <murphy/core/console.h>
#include <murphy/core/event.h>
#include <murphy/core/context.h>
#include <murphy/core/lua-bindings/murphy.h>

#include <murphy-db/mqi.h>

#include <murphy/resource/config-api.h>
#include <murphy/resource/manager-api.h>
#include <murphy/resource/client-api.h>
#include <murphy/resource/protocol.h>

#include "plugin-gam-resource-manager.h"
#include "backend.h"
#include "source.h"
#include "sink.h"
#include "usecase.h"

typedef struct dependency_s   dependency_t;

enum {
    CONFDIR,
    PREFIX,
    DECISION_NAMES,
    MAX_ACTIVE,
};

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
        config->confdir    = mrp_strdup(args[CONFDIR].str);
        config->prefix     = mrp_strdup(args[PREFIX].str);
        config->confnams   = mrp_strdup(args[DECISION_NAMES].str);
        config->max_active = args[MAX_ACTIVE].i32;
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


#define MANAGER_DESCRIPTION "Plugin to implement GAM resources"
#define MANAGER_HELP        "Maybe later ..."
#define MANAGER_VERSION      MRP_VERSION_INT(0, 0, 1)
#define MANAGER_AUTHORS     "Janos Kovacs <jankovac503@gmail.com>"

#define STRING_ARG(_id,_n,_d) MRP_PLUGIN_ARGIDX(_id, STRING, _n, _d)
#define INTEGER_ARG(_id,_n,_d) MRP_PLUGIN_ARGIDX(_id, INT32, _n, _d)

static mrp_plugin_arg_t arg_defs[] = {
    STRING_ARG  (CONFDIR       , "config_dir"    , MRP_RESMGR_DEFAULT_CONFDIR),
    STRING_ARG  (PREFIX        , "prefix"        , MRP_RESMGR_DEFAULT_PREFIX ),
    STRING_ARG  (DECISION_NAMES, "decision_names", MRP_RESMGR_DEFAULT_NAMES  ),
    INTEGER_ARG (MAX_ACTIVE    , "max_active"    , 1                         ),
};

#undef INTEGER_ARG
#undef STRING_ARG

MURPHY_REGISTER_PLUGIN("gam-resource-manager",
                       MANAGER_VERSION,
                       MANAGER_DESCRIPTION,
                       MANAGER_AUTHORS,
                       MANAGER_HELP,
                       MRP_SINGLETON,
                       manager_init,
                       manager_exit,
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
