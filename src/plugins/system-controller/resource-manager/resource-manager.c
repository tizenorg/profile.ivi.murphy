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

#include "resource-manager.h"
#include "screen.h"
#include "audio.h"
#include "notifier.h"

static int hash_compare(const void *, const void *);
static uint32_t hash_function(const void *);

mrp_resmgr_t *mrp_resmgr_create(void)
{
    mrp_resmgr_t *resmgr;
    mrp_htbl_config_t cfg;

    if (!(resmgr = mrp_allocz(sizeof(mrp_resmgr_t)))) {
        mrp_log_error("system-controller: failed to allocate private data "
                      "resource manager");
        return NULL;
    }

    cfg.nentry = MRP_RESMGR_RESOURCE_MAX;
    cfg.comp = hash_compare;
    cfg.hash = hash_function;
    cfg.free = NULL;
    cfg.nbucket = MRP_RESMGR_RESOURCE_BUCKETS;

    resmgr->resources = mrp_htbl_create(&cfg);
    resmgr->screen = mrp_resmgr_screen_create(resmgr);
    resmgr->audio = mrp_resmgr_audio_create(resmgr);
    resmgr->notifier = mrp_resmgr_notifier_create(resmgr);

    return resmgr;
}

void mrp_resmgr_insert_resource(mrp_resmgr_t *resmgr,
                                mrp_zone_t *zone,
                                mrp_resource_t *key,
                                void *resource)
{
    uint32_t zoneid;

    MRP_ASSERT(resmgr && zone && key && resource, "invalid argument");
    MRP_ASSERT(resmgr->resources, "uninitialised data structure");

    zoneid = mrp_zone_get_id(zone);

    resmgr->zones |= ((mrp_zone_mask_t)1 << zoneid);

    mrp_htbl_insert(resmgr->resources, key, resource);
}

void *mrp_resmgr_remove_resource(mrp_resmgr_t *resmgr,
                                 mrp_zone_t *zone,
                                 mrp_resource_t *key)
{
    MRP_ASSERT(resmgr && zone && key, "invalid argument");
    MRP_ASSERT(resmgr->resources, "uninitialised data structure");

    return mrp_htbl_remove(resmgr->resources, key, FALSE);
}

void *mrp_resmgr_lookup_resource(mrp_resmgr_t *resmgr, mrp_resource_t *key)
{
    MRP_ASSERT(resmgr && key, "invalid argument");
    MRP_ASSERT(resmgr->resources, "uninitialised data structure");

    return mrp_htbl_lookup(resmgr->resources, key);
}

int mrp_resmgr_disable_print(mrp_resmgr_disable_t disable, char *buf, int len)
{
#define PRINT(...) \
    do { p += snprintf(p, e-p, __VA_ARGS__); if (p>=e) return p-buf; } while(0)

    typedef struct {
        const char *name;
        mrp_resmgr_disable_t mask;
    } bit_t;

    static bit_t bits[] = {
        { "requisite", MRP_RESMGR_DISABLE_REQUISITE },
        { "appid"    , MRP_RESMGR_DISABLE_APPID     },
        {  NULL      , 0                            }
    };

    char *p, *e;
    char *sep;
    bit_t *bit;

    MRP_ASSERT(buf && len > 0, "invalid argument");

    e = (p = buf) + len;

    PRINT("0x%02x-", disable);

    if (!disable)
        PRINT("none");
    else {
        for (bit = bits, sep = "";   bit->name && disable;   bit++) {
            if ((disable & bit->mask)) {
                PRINT("%s%s", sep, bit->name);
                sep = "|";
                disable &= ~bit->mask;
            }
        }

        if (disable)
            PRINT("%s<unknown 0x%02x>", sep, disable);
    }

    return p - buf;

#undef PRINT
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
    return (uint32_t)(((ptrdiff_t)key >> 4) & (ptrdiff_t)0xffffffff);
}
