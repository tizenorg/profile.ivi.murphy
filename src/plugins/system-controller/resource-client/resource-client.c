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

#include <murphy/common.h>
#include <murphy/common/debug.h>
#include <murphy/core/plugin.h>
#include <murphy/core/console.h>
#include <murphy/core/event.h>
#include <murphy/core/context.h>


#include <murphy/resource/config-api.h>
#include <murphy/resource/manager-api.h>
#include <murphy/resource/client-api.h>
#include <murphy/resource/protocol.h>

#include "resource-client.h"
#include "wayland/wayland.h"

static int hash_compare(const void *, const void *);
static uint32_t hash_function(const void *);

static void event_cb(uint32_t, mrp_resource_set_t *, void *);

static const char *type_str(mrp_resclnt_resource_set_type_t);

mrp_resclnt_t *mrp_resclnt_create(void)
{
    mrp_resclnt_t *resclnt;
    mrp_htbl_config_t cfg;

    if (!(resclnt = mrp_allocz(sizeof(mrp_resclnt_t)))) {
        mrp_log_error("system-controller: failed to allocate private data "
                      "resource manager");
        return NULL;
    }

    cfg.nentry = MRP_RESCLNT_RESOURCE_SET_MAX;
    cfg.comp = hash_compare;
    cfg.hash = hash_function;
    cfg.free = NULL;
    cfg.nbucket = MRP_RESCLNT_RESOURCE_SET_BUCKETS;

    resclnt->client = mrp_resource_client_create("system-controller", resclnt);
    resclnt->rsets.screen = mrp_htbl_create(&cfg);
    resclnt->rsets.audio  = mrp_htbl_create(&cfg);
    resclnt->rsets.input  = mrp_htbl_create(&cfg);

    if (!resclnt->client) {
        mrp_log_error("system-controller: failed to create resource client");
        mrp_resclnt_destroy(resclnt);
        return NULL;
    }

    return resclnt;
}

void mrp_resclnt_destroy(mrp_resclnt_t *resclnt)
{
    if (resclnt) {
        mrp_resource_client_destroy(resclnt->client);

        mrp_htbl_destroy(resclnt->rsets.screen, false);
        mrp_htbl_destroy(resclnt->rsets.audio, false);
        mrp_htbl_destroy(resclnt->rsets.input, false);

        mrp_free(resclnt);
    }
}

bool mrp_resclnt_add_resource_set(mrp_resclnt_t *resclnt,
                                  mrp_resclnt_resource_set_type_t type,
                                  const char *zone_name,
                                  const char *appid,
                                  void *key)
{
    mrp_application_t *app;
    mrp_resource_set_t *rset;
    const char *name;
    bool shared;
    mrp_htbl_t *htbl;
    mrp_attr_t attr[16];
    int sts;

    MRP_ASSERT(resclnt && resclnt->client && zone_name && key,
               "invalid argument");
    MRP_ASSERT(resclnt->rsets.screen && resclnt->rsets.audio &&
               resclnt->rsets.input, "uninitialised data structure");

    if (!(app = mrp_application_find(appid)) &&
        !(app = mrp_application_find(MRP_SYSCTL_APPID_DEFAULT)))
    {
        mrp_debug("failed to add %s resource set: can't find "
                  "application '%s'", type_str(type), appid);
        return false;
    }

    memset(attr, 0, sizeof(attr));

    switch (type) {

    case MRP_RESCLIENT_RESOURCE_SET_SCREEN:
        name = MRP_SYSCTL_SCREEN_RESOURCE;
        htbl = resclnt->rsets.screen;
        shared = true;

        attr[0].name = "classpri";
        attr[0].type = mqi_integer;
        attr[0].value.integer = app->screen_priority;

        attr[1].name = "area";
        attr[1].type = mqi_string;
        attr[1].value.string = app->area_name;

        attr[2].name = "appid";
        attr[2].type = mqi_string;
        attr[2].value.string = appid;

        attr[3].name = "surface";
        attr[3].type = mqi_integer;
        attr[3].value.integer = (int32_t)(key - NULL);
        break;

    case MRP_RESCLIENT_RESOURCE_SET_AUDIO:
        name = MRP_SYSCTL_AUDIO_RESOURCE;
        htbl = resclnt->rsets.audio;
        shared = false;
        break;

    case MRP_RESCLIENT_RESOURCE_SET_INPUT:
        name = MRP_SYSCTL_INPUT_RESOURCE;
        htbl = resclnt->rsets.input;
        shared = true;
        break;

    default:
        mrp_log_error("system-controller: failed to add resource: "
                      "invalid resource type %d", type);
        return false;
    }

    rset = mrp_resource_set_create(resclnt->client, false, false, 0,
                                   event_cb, resclnt);
    if (!rset) {
        mrp_debug("system-controller: failed to add %s resource set: "
                  "can't create resource set", type_str(type));
        return false;
    }

    sts = mrp_resource_set_add_resource(rset, name, shared, attr, true);

    if (sts < 0) {
        mrp_log_error("system-controller: failed to add %s resource set: "
                      "can't create resource '%s'", type_str(type), name);
        mrp_resource_set_destroy(rset);
        return false;
    }

    sts = mrp_application_class_add_resource_set(app->resource_class,
                                                 zone_name, rset,
                                                 ++(resclnt->reqno));
    if (sts < 0) {
        mrp_log_error("system-controller: failed to add %s resource set: "
                      "can't add resource to application class",
                      type_str(type));
        mrp_resource_set_destroy(rset);
        return false;
    }

    mrp_htbl_insert(htbl, key, rset);

    return true;
}

void mrp_resclnt_remove_resource_set(mrp_resclnt_t *resclnt,
                                     mrp_resclnt_resource_set_type_t type,
                                     void *key)
{
    mrp_htbl_t *htbl;
    mrp_resource_set_t *rset;

    MRP_ASSERT(resclnt && key, "invalid argument");
    MRP_ASSERT(resclnt->rsets.screen && resclnt->rsets.audio &&
               resclnt->rsets.input, "uninitialised data structure");


    switch (type) {

    case MRP_RESCLIENT_RESOURCE_SET_SCREEN:
        htbl = resclnt->rsets.screen;
        break;

    case MRP_RESCLIENT_RESOURCE_SET_AUDIO:
        htbl = resclnt->rsets.audio;
        break;

    case MRP_RESCLIENT_RESOURCE_SET_INPUT:
        htbl = resclnt->rsets.input;
        break;

    default:
        mrp_log_error("system-controller: failed to remove resource: "
                      "invalid resource type %d", type);
        return;
    }

    if (!(rset = mrp_htbl_remove(htbl, key, false))) {
        mrp_log_error("system-controller: failed to remove %s resource set: "
                      "can't find it", type_str(type));
        return;
    }

    mrp_resource_set_release(rset, ++(resclnt->reqno));
    mrp_resource_set_destroy(rset);
}

bool mrp_resclnt_acquire_resource_set(mrp_resclnt_t *resclnt,
                                      mrp_resclnt_resource_set_type_t type,
                                      void *key)
{
    mrp_resource_set_t *rset;
    mrp_htbl_t *htbl;

    MRP_ASSERT(resclnt && resclnt->client, "invalid argument");
    MRP_ASSERT(resclnt->rsets.screen && resclnt->rsets.audio &&
               resclnt->rsets.input, "uninitialised data structure");

    switch (type) {

    case MRP_RESCLIENT_RESOURCE_SET_SCREEN:
        htbl = resclnt->rsets.screen;
        break;

    case MRP_RESCLIENT_RESOURCE_SET_AUDIO:
        htbl = resclnt->rsets.audio;
        break;

    case MRP_RESCLIENT_RESOURCE_SET_INPUT:
        htbl = resclnt->rsets.input;
        break;

    default:
        mrp_log_error("system-controller: failed to acquire resource: "
                      "invalid resource type %d", type);
        return false;
    }

    if (!(rset = mrp_htbl_lookup(htbl, key))) {
        mrp_log_error("system-controller: failed to acquire resource set: "
                      "can't find it (key=%p)", key);
        return false;
    }

    mrp_resource_set_acquire(rset, ++(resclnt->reqno));

    return true;
}


bool mrp_resclnt_release_resource_set(mrp_resclnt_t *resclnt,
                                      mrp_resclnt_resource_set_type_t type,
                                      void *key)
{
    mrp_resource_set_t *rset;
    mrp_htbl_t *htbl;

    MRP_ASSERT(resclnt && resclnt->client, "invalid argument");
    MRP_ASSERT(resclnt->rsets.screen && resclnt->rsets.audio &&
               resclnt->rsets.input, "uninitialised data structure");

    switch (type) {

    case MRP_RESCLIENT_RESOURCE_SET_SCREEN:
        htbl = resclnt->rsets.screen;
        break;

    case MRP_RESCLIENT_RESOURCE_SET_AUDIO:
        htbl = resclnt->rsets.audio;
        break;

    case MRP_RESCLIENT_RESOURCE_SET_INPUT:
        htbl = resclnt->rsets.input;
        break;

    default:
        mrp_log_error("system-controller: failed to acquire resource: "
                      "invalid resource type %d", type);
        return false;
    }

    if (!(rset = mrp_htbl_lookup(htbl, key))) {
        mrp_log_error("system-controller: failed to release resource set: "
                      "can't find it (key=%p)", key);
        return false;
    }

    mrp_resource_set_release(rset, ++(resclnt->reqno));

    return true;
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
    return (uint32_t)(((ptrdiff_t)key >> 3) & (ptrdiff_t)0xffffffff);
}


static void event_cb(uint32_t reqid, mrp_resource_set_t *rset, void *u)
{
    mrp_resource_client_t *resclnt = (mrp_resource_client_t *)u;

    MRP_UNUSED(reqid);
    MRP_UNUSED(rset);
    MRP_UNUSED(resclnt);
}

static const char *type_str(mrp_resclnt_resource_set_type_t type)
{
    switch (type) {
    case MRP_RESCLIENT_RESOURCE_SET_SCREEN:    return "screen";
    case MRP_RESCLIENT_RESOURCE_SET_AUDIO:     return "audio";
    case MRP_RESCLIENT_RESOURCE_SET_INPUT:     return "input";
    default:                                   return "<unknown>";
    }
}
