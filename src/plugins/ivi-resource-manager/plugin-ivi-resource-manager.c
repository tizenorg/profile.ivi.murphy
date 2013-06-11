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

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>
#include <murphy/common/mainloop.h>
#include <murphy/common/msg.h>
#include <murphy/common/transport.h>
#include <murphy/common/debug.h>
#include <murphy/core/plugin.h>
#include <murphy/core/console.h>
#include <murphy/core/event.h>
#include <murphy/core/lua-bindings/murphy.h>

#include <murphy-db/mql.h>
#include <murphy-db/mqi.h>

#include <murphy/resource/config-api.h>
#include <murphy/resource/manager-api.h>
#include <murphy/resource/protocol.h>


typedef struct {
    mrp_plugin_t      *plugin;
    mrp_event_watch_t *w;
} manager_data_t;


static void resource_event_handler(uint32_t, mrp_resource_set_t *, void *);



#if 0
static int set_default_configuration(void)
{
    typedef struct {
        const char     *name;
        bool            share;
        mrp_attr_def_t *attrs;
    } resdef_t;

    static const char *zones[] = {
        "driver",
        "front-passenger",
        "rear-left-passenger",
        "rear-right-passenger",
        NULL
    };

    static const char *classes[] = {
        "implicit",
        "player",
        "game",
        "phone",
        "navigator",
        NULL
    };

    static mrp_attr_def_t audio_attrs[] = {
        { "role", MRP_RESOURCE_RW, mqi_string , .value.string="music" },
        {  NULL ,        0       , mqi_unknown, .value.string=NULL    }
    };

    static resdef_t  resources[] = {
        { "audio_playback" , true , audio_attrs  },
        { "audio_recording", true , NULL         },
        { "video_playback" , false, NULL         },
        { "video_recording", false, NULL         },
        {      NULL        , false, NULL         }
    };

    const char *name;
    resdef_t *rdef;
    uint32_t i;

    mrp_zone_definition_create(NULL);

    for (i = 0;  (name = zones[i]);  i++)
        mrp_zone_create(name, NULL);

    for (i = 0;  (name = classes[i]); i++)
        mrp_application_class_create(name, i);

    for (i = 0;  (rdef = resources + i)->name;  i++) {
        mrp_resource_definition_create(rdef->name, rdef->share, rdef->attrs,
                                       NULL, NULL);
    }

    return 0;
}
#endif

static void event_cb(mrp_event_watch_t *w, int id, mrp_msg_t *event_data,
                     void *user_data)
{
    mrp_plugin_t     *plugin   = (mrp_plugin_t *)user_data;
#if 0
    mrp_plugin_arg_t *args     = plugin->args;
#endif
    manager_data_t   *data     = (manager_data_t *)plugin->data;
    const char       *event    = mrp_get_event_name(id);
    uint16_t          tag_inst = MRP_PLUGIN_TAG_INSTANCE;
    uint16_t          tag_name = MRP_PLUGIN_TAG_PLUGIN;
    const char       *inst;
    const char       *name;
    int               success;

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
#if 0
                    set_default_configuration();
                    mrp_log_info("%s: built-in default configuration "
                                 "is in use", plugin->instance);
#endif
                }
            }
        } /* if PLUGIN_STARTED */
    }
}


static int subscribe_events(mrp_plugin_t *plugin)
{
    manager_data_t   *data = (manager_data_t *)plugin->data;
    mrp_event_mask_t  events;

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
    manager_data_t *data = (manager_data_t *)plugin->data;

    if (data->w) {
        mrp_del_event_watch(data->w);
        data->w = NULL;
    }
}



static int manager_init(mrp_plugin_t *plugin)
{
#if 0
    mrp_plugin_arg_t *args = plugin->args;
#endif
    manager_data_t   *data;

    mrp_log_info("%s() called for IVI resource manager instance '%s'...",
                 __FUNCTION__, plugin->instance);

    if (!(data = mrp_allocz(sizeof(*data)))) {
        mrp_log_error("Failed to allocate private data for IVI resource "
                      "manager plugin instance %s.", plugin->instance);
        return FALSE;
    }

    data->plugin = plugin;

    plugin->data = data;

    subscribe_events(plugin);

    return TRUE;
}


static void manager_exit(mrp_plugin_t *plugin)
{
    mrp_log_info("%s() called for IVI resource manager instance '%s'...",
                 __FUNCTION__, plugin->instance);

    unsubscribe_events(plugin);
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
#if 0
                       exports, MRP_ARRAY_SIZE(exports),
                       imports, MRP_ARRAY_SIZE(imports),
#else
                       NULL, 0,
                       NULL, 0,
#endif
                       NULL);

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
