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
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include <murphy/common.h>
#include <murphy/core.h>

#include "telephony/resctl.h"
#include "telephony/ofono.h"
#include "telephony/telephony.h"

enum {
    ARG_ZONE,                            /* resource set zone */
    ARG_CLASS,                           /* application class */
    ARG_PRIORITY,                        /* priority within class */
    ARG_PLAYBACK,                        /* playback resource config */
    ARG_RECORDING,                       /* recording resource config */
    ARG_ROLE,                            /* (media) role attribute */
};

mrp_plugin_t *telephony_plugin;



static int telephony_init(mrp_plugin_t *plugin)
{
    mrp_context_t    *ctx  = plugin->ctx;
    mrp_plugin_arg_t *args = plugin->args;
    const char       *zone = args[ARG_ZONE].str;
    const char       *cls  = args[ARG_CLASS].str;
    uint32_t          prio = args[ARG_PRIORITY].u32;
    const char       *play = args[ARG_PLAYBACK].str;
    const char       *rec  = args[ARG_RECORDING].str;
    const char       *role = args[ARG_ROLE].str;

    if (!resctl_config(zone, cls, prio, play, rec, role)) {
        mrp_log_error("Failed to configure telephony resource-set.");

        return FALSE;
    }

    if (ctx != NULL) {
        /* use the mainloop from the plugin context */
        return tel_start_listeners(ctx->ml);
    }
    return FALSE;
}


static void telephony_exit(mrp_plugin_t *plugin)
{
    MRP_UNUSED(plugin);

    tel_exit_listeners();
}


#define TELEPHONY_VERSION MRP_VERSION_INT(0, 1, 0)

#define TELEPHONY_DESCRIPTION "A telephony plugin for Murphy."

#define TELEPHONY_AUTHORS "Zoltan Kis <zoltan.kis@intel.com>"

#define TELEPHONY_HELP \
    "The telephony plugin follows ofono DBUS activity" \
    "and updates Murphy database with telephony calls information"

#define DEFAULT_ZONE      "driver"
#define DEFAULT_CLASS     "phone"
#define DEFAULT_PRIORITY  1
#define DEFAULT_PLAYBACK  "audio_playback,mandatory,shared"
#define DEFAULT_RECORDING "audio_recording,mandatory,exclusive"
#define DEFAULT_ROLE      "phone"

static mrp_plugin_arg_t telephony_args[] = {
    MRP_PLUGIN_ARGIDX(ARG_ZONE     , STRING, "zone"     , DEFAULT_ZONE),
    MRP_PLUGIN_ARGIDX(ARG_CLASS    , STRING, "class"    , DEFAULT_CLASS),
    MRP_PLUGIN_ARGIDX(ARG_PRIORITY , UINT32, "priority" , DEFAULT_PRIORITY),
    MRP_PLUGIN_ARGIDX(ARG_PLAYBACK , STRING, "playback" , DEFAULT_PLAYBACK),
    MRP_PLUGIN_ARGIDX(ARG_RECORDING, STRING, "recording", DEFAULT_RECORDING),
    MRP_PLUGIN_ARGIDX(ARG_ROLE     , STRING, "role"     , DEFAULT_ROLE),
};

MRP_REGISTER_CORE_PLUGIN("telephony",
                         TELEPHONY_VERSION, TELEPHONY_DESCRIPTION,
                         TELEPHONY_AUTHORS, TELEPHONY_HELP, MRP_SINGLETON,
                         telephony_init, telephony_exit,
                         telephony_args, MRP_ARRAY_SIZE(telephony_args),
                         NULL, 0, /* exports  */
                         NULL, 0, /* imports  */
                         NULL     /* commands */ );
