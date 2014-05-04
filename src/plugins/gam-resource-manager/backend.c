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
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <murphy/common.h>

#include <murphy-db/mqi.h>

#include <murphy/resource/config-api.h>
#include <murphy/resource/manager-api.h>
#include <murphy/resource/client-api.h>

#include "backend.h"
#include "source.h"
#include "sink.h"
#include "usecase.h"

#define ANY_ZONE  (~((uint32_t)0))


#define ATTRIBUTE(n,t,v)    {n, MRP_RESOURCE_RW, mqi_##t, {.t=v}}
#define ATTR_END            {NULL, 0, 0, {.string=NULL}}


typedef enum   decision_value_e  decision_value_t;
typedef enum   state_value_e     state_value_t;
typedef struct resource_s        resource_t;
typedef struct resource_type_s   resource_type_t;
typedef struct decision_s        decision_t;
typedef struct state_s           state_t;

enum decision_value_e {
    STATE_ERROR = -1,

    STATE_STOP = 0,
    STATE_PAUSE,
    STATE_PLAY,

    STATE_MAX
};

enum state_value_e {
    DECISION_ERROR = -1,

    DECISION_TEARDOWN = 0,
    DECISION_DISCONNECTED,
    DECISION_CONNECTED,
    DECISION_SUSPENDED,

    DECISION_MAX
};

struct resource_type_s {
    int id;
    const char *name;
    uint32_t resid;
};

struct mrp_resmgr_backend_s {
    mrp_resmgr_t *resmgr;
    resource_type_t types[MRP_RESMGR_RESOURCE_TYPE_MAX];
    struct {
        mrp_htbl_t *by_pointer;
        mrp_htbl_t *by_connid;
    } resources;
};

struct decision_s {
    int32_t new;
    int32_t current;
};

struct state_s {
    int32_t new;
    int32_t current;
};

struct mrp_resmgr_resource_s {
    const char *name;
    mrp_resmgr_backend_t *backend;
    mrp_resource_t *res;
    resource_type_t *type;
    mrp_resmgr_source_t *source;
    mrp_resmgr_sink_t *sink;
    mrp_list_hook_t source_link;
    uint32_t zoneid;
    uint32_t connid;
    uint32_t connno;
    decision_t decision;
    state_t state;
};


static void make_resource_definition(mrp_resmgr_backend_t *, int, const char*);
static resource_type_t *find_resource_definition_by_id(mrp_resmgr_backend_t *,
                                                       int);

static int hash_compare(const void *, const void *);
static uint32_t ptr_hash_function(const void *);
static uint32_t id_hash_function(const void *);

//static const char *get_resource_appid(mrp_resource_t *);
static int32_t get_resource_sourceid(mrp_resource_t *);
static int32_t get_resource_sinkid(mrp_resource_t *);
static int32_t get_resource_connid(mrp_resource_t *);
static int32_t get_resource_connno(mrp_resource_t *);
static int32_t get_resource_stamp(mrp_resource_t *);

static bool set_resource_source_and_sink(mrp_resource_t *,
                                         mrp_resmgr_source_t *,
                                         mrp_resmgr_sink_t *);
static bool set_resource_stamp(mrp_resource_t *, int32_t);
static bool set_resource_decision(mrp_resource_t *, int32_t);


static void resource_create(mrp_resmgr_backend_t *, mrp_application_class_t *,
                            mrp_zone_t *, mrp_resource_t *);
static void resource_destroy(mrp_resmgr_backend_t *, mrp_zone_t *,
                             mrp_resource_t *);
static bool resource_acquire(mrp_resmgr_backend_t *, mrp_zone_t *,
                             mrp_resource_t *);
static bool resource_release(mrp_resmgr_backend_t *, mrp_zone_t *,
                             mrp_resource_t *);

static bool resource_register_by_id(mrp_resmgr_backend_t *,
                                    mrp_resmgr_resource_t *);
static mrp_resmgr_resource_t *resource_lookup_by_pointer(mrp_resmgr_backend_t*,
                                                         mrp_resource_t *);

static size_t resource_print_name(mrp_resmgr_source_t *,uint32_t,char *,size_t);


static void make_decisions(mrp_resmgr_backend_t *);
static void commit_decisions(mrp_resmgr_backend_t *);
static size_t print_decision(mrp_resmgr_resource_t *, char *, size_t);
static size_t print_commit(mrp_resmgr_resource_t *, char *, size_t);

static void backend_notify(mrp_resource_event_t, mrp_zone_t *,
                           mrp_application_class_t *, mrp_resource_t *, void*);
static void backend_init(mrp_zone_t *, void *);
static bool backend_allocate(mrp_zone_t *, mrp_resource_t *, void *);
static void backend_free(mrp_zone_t *, mrp_resource_t *, void *);
static bool backend_advice(mrp_zone_t *, mrp_resource_t *, void *);
static void backend_commit(mrp_zone_t *, void *);



#define APPID_ATTRIDX      0
#define ROLE_ATTRIDX       1
#define PID_ATTRIDX        2
#define POLICY_ATTRIDX     3
#define SRCNAM_ATTRIDX     4
#define SRCID_ATTRIDX      5
#define SINKNAM_ATTRIDX    6
#define SINKID_ATTRIDX     7
#define CONNID_ATTRIDX     8
#define CONNNO_ATTRIDX     9
#define STAMP_ATTRIDX     10
#define DECISION_ATTRIDX  11

#define ATTR_MAX          12

static mrp_attr_def_t audio_attrs[] = {
    ATTRIBUTE( "appid"      , string ,  "<undefined>" ),
    ATTRIBUTE( "role"       , string ,  "music"       ),
    ATTRIBUTE( "pid"        , string ,  "<unknown>"   ),
    ATTRIBUTE( "policy"     , string ,  "relaxed"     ),
    ATTRIBUTE( "source_name", string ,  "<undefined>" ),
    ATTRIBUTE( "source_id"  , integer,  0             ),
    ATTRIBUTE( "sink_name"  , string ,  "<undefined>" ),
    ATTRIBUTE( "sink_id"    , integer,  0             ),
    ATTRIBUTE( "connid"     , integer,  0             ),
    ATTRIBUTE( "connno"     , integer,  0             ),
    ATTRIBUTE( "stamp"      , integer,  0             ),
    ATTRIBUTE( "decision"   , string ,  "<not yet>"   ),
    ATTR_END
};

static mrp_resource_mgr_ftbl_t playback_ftbl = {
    backend_notify,             /* notify   */
    backend_init,               /* init     */
    backend_allocate,           /* allocate */
    backend_free,               /* free     */
    backend_advice,             /* advice   */
    NULL                        /* commit   */
};

static mrp_resource_mgr_ftbl_t recording_ftbl = {
    backend_notify,             /* notify   */
    NULL,                       /* init     */
    backend_allocate,           /* allocate */
    backend_free,               /* free     */
    backend_advice,             /* advice   */
    backend_commit              /* commit   */
};

static mrp_resource_mgr_ftbl_t *backend_ftbl[MRP_RESMGR_RESOURCE_TYPE_MAX] = {
    [ MRP_RESMGR_RESOURCE_TYPE_PLAYBACK  ] = &playback_ftbl ,
    [ MRP_RESMGR_RESOURCE_TYPE_RECORDING ] = &recording_ftbl,
};


static const char *decision_names[DECISION_MAX + 1] = {
    [ DECISION_TEARDOWN     ] = "teardown"    ,
    [ DECISION_DISCONNECTED ] = "disconnected",
    [ DECISION_CONNECTED    ] = "connected"   ,
    [ DECISION_SUSPENDED    ] = "suspended"   ,
};

static const char *state_names[STATE_MAX + 1] = {
    [ STATE_STOP  ] = "stop" ,
    [ STATE_PAUSE ] = "pause",
    [ STATE_PLAY  ] = "play" ,
};

static state_value_t decision2state[DECISION_MAX] = {
    [ DECISION_TEARDOWN     ] = STATE_STOP ,
    [ DECISION_DISCONNECTED ] = STATE_STOP ,
    [ DECISION_CONNECTED    ] = STATE_PLAY ,
    [ DECISION_SUSPENDED    ] = STATE_PAUSE,
};


mrp_resmgr_backend_t *mrp_resmgr_backend_create(mrp_resmgr_t *resmgr)
{
    mrp_resmgr_backend_t *backend;
    mrp_htbl_config_t pcfg, icfg;

    MRP_ASSERT(resmgr, "invalid argument");

    if ((backend = mrp_allocz(sizeof(mrp_resmgr_backend_t)))) {
        pcfg.nentry = MRP_RESMGR_RESOURCE_MAX;
        pcfg.comp = hash_compare;
        pcfg.hash = ptr_hash_function;
        pcfg.free = NULL;
        pcfg.nbucket = MRP_RESMGR_RESOURCE_BUCKETS;

        icfg.nentry = MRP_RESMGR_RESOURCE_MAX;
        icfg.comp = hash_compare;
        icfg.hash = id_hash_function;
        icfg.free = NULL;
        icfg.nbucket = MRP_RESMGR_RESOURCE_BUCKETS;

        backend->resmgr = resmgr;
        backend->resources.by_pointer = mrp_htbl_create(&pcfg);
        backend->resources.by_connid = mrp_htbl_create(&icfg);

        make_resource_definition(backend, MRP_RESMGR_RESOURCE_TYPE_PLAYBACK,
                                 MRP_RESMGR_PLAYBACK_RESOURCE);
        make_resource_definition(backend, MRP_RESMGR_RESOURCE_TYPE_RECORDING,
                                 MRP_RESMGR_RECORDING_RESOURCE);
    }

    return backend;
}

void mrp_resmgr_backend_destroy(mrp_resmgr_backend_t *backend)
{
    if (backend) {
        mrp_free(backend);
    }
}

const char **mrp_resmgr_backend_get_decision_names(void)
{
    return decision_names;
}

uint32_t mrp_resmgr_backend_get_resource_connid(mrp_resmgr_resource_t *ar)
{
    MRP_ASSERT(ar, "invalid argument");

    return ar->connid;
}

uint32_t mrp_resmgr_backend_get_resource_connno(mrp_resmgr_resource_t *ar)
{
    MRP_ASSERT(ar, "invalid argument");

    return ar->connno;
}

int32_t mrp_resmgr_backend_get_resource_state(mrp_resmgr_resource_t *ar)
{
    MRP_ASSERT(ar, "invalid argument");

    if (get_resource_stamp(ar->res) == 0)
        return 0;

    return ar->state.current;
}

int32_t mrp_resmgr_backend_get_resource_decision_id(mrp_resmgr_resource_t *ar)
{
    MRP_ASSERT(ar, "invalid argument");

    if (get_resource_stamp(ar->res) == 0)
        return 0;

    return mrp_resmgr_sink_get_decision_id(ar->sink, ar->source);
}


uint32_t mrp_resmgr_backend_get_attribute_index(const char *name,
                                                mqi_data_type_t type)
{
    mrp_attr_def_t *attrd;
    uint32_t idx;

    if (name) {
        for (idx = 0;  (attrd = audio_attrs + idx)->name;  idx++) {
            if (!strcmp(name, attrd->name)) {
                if (attrd->type == type)
                    return idx;
                break;
            }
        } /* for attrd */
    }

    return MRP_RESMGR_RESOURCE_FIELD_INVALID;
}


int32_t mrp_resmgr_backend_get_integer_attribute(mrp_resmgr_resource_t *ar,
                                                 uint32_t idx)
{
    mrp_attr_t attr;

    if (mrp_resource_read_attribute(ar->res, idx, &attr)) {
        if (attr.type == mqi_integer)
            return attr.value.integer;
    }

    return 0;
}

const char *mrp_resmgr_backend_get_string_attribute(mrp_resmgr_resource_t *ar,
                                                    uint32_t idx)
{
    mrp_attr_t attr;

    if (mrp_resource_read_attribute(ar->res, idx, &attr)) {
        if (attr.type == mqi_string)
            return attr.value.string;
    }

    return "";
}


mrp_resmgr_resource_t *mrp_resmgr_backend_resource_list_entry(
                                                   mrp_list_hook_t *entry)
{
    MRP_ASSERT(entry, "invalid argument");

    return mrp_list_entry(entry, mrp_resmgr_resource_t, source_link);
}






#if 0
int mrp_resmgr_backend_print(mrp_resmgr_backend_t *backend,
                           uint32_t zoneid,
                           char *buf, int len)
{
#define PRINT(...)                              \
    do {                                        \
        p += snprintf(p, e-p, __VA_ARGS__);     \
        if (p >= e)                             \
            return p - buf;                     \
    } while (0)

    char *p, *e;
    uint32_t grantid;
    mrp_list_hook_t *resources, *rentry, *rn;
    mrp_resmgr_resource_t *ar;
    mrp_attr_t a;
    size_t i;
    char disable[256];
    char requisite[1024];

    MRP_ASSERT(backend && buf && len > 0, "invalid argument");

    e = (p = buf) + len;
    *p = 0;

    if (zoneid < MRP_ZONE_MAX) {
        resources = backend->zones + zoneid;
        grantid = backend->grantids[zoneid];
    }
    else {
        resources = NULL;
        grantid = 0;
    }

    PRINT("      Resource '%s' - grantid:%u\n",
          MRP_SYSCTL_AUDIO_RESOURCE, grantid);

    if (!resources || mrp_list_empty(resources))
        PRINT("         No resources\n");
    else {
        mrp_list_foreach_back(resources, rentry, rn) {
            ar = mrp_list_entry(rentry, mrp_resmgr_resource_t, link);

            mrp_resmgr_disable_print(ar->disable, disable,
                                     sizeof(disable));
            mrp_application_requisite_print(ar->requisite, requisite,
                                            sizeof(requisite));

            PRINT("            "
                  "key:0x%08x %s %s grantid:%u requisite:%s disable:%s",
                  ar->key,
                  ar->interrupt ? "interrupt" : "base",
                  ar->acquire ? "acquire":"release",
                  ar->grantid,
                  requisite,
                  disable);

            for (i = 0;  i < MRP_ARRAY_SIZE(audio_attrs) - 1;  i++) {
                if ((mrp_resource_read_attribute(ar->res, i, &a))) {
                    PRINT(" %s:", a.name);

                    switch (a.type) {
                    case mqi_string:   PRINT("'%s'",a.value.string); break;
                    case mqi_integer:  PRINT("%d",a.value.integer);  break;
                    case mqi_unsignd:  PRINT("%u",a.value.unsignd);  break;
                    case mqi_floating: PRINT("%lf",a.value.floating);break;
                    default:           PRINT("<unsupported type>");  break;
                    }
                }
            }

            PRINT("\n");
        } /* mrp_list_foreach_back - resources */
    }

    return p - buf;
}
#endif

static void make_resource_definition(mrp_resmgr_backend_t *backend,
                                     int id,
                                     const char *name)
{
    resource_type_t *type = backend->types + id;

    MRP_ASSERT(backend && id >= 0 && id < MRP_RESMGR_RESOURCE_TYPE_MAX,
               "invalid attribute");

    type->id = id;
    type->name = mrp_strdup(name);
    type->resid = mrp_resource_definition_create(type->name, true, /* share */
                                                 audio_attrs, backend_ftbl[id],
                                                 backend);

    mrp_lua_resclass_create_from_c(type->resid);
}

static resource_type_t *find_resource_definition_by_id(
                                             mrp_resmgr_backend_t *backend,
                                             int id)
{
    resource_type_t *type;
    int i;

    MRP_ASSERT(backend, "invalid argument");

    for (i = 0;  i < MRP_RESMGR_RESOURCE_TYPE_MAX;  i++) {
        type = backend->types + i;

        if (type->id == id)
            return type;
    }

    return NULL;
}

static int hash_compare(const void *key1, const void *key2)
{
    if (key1 < key2)
        return -1;
    if (key1 > key2)
        return 1;
    return 0;
}

static uint32_t ptr_hash_function(const void *key)
{
    return (uint32_t)(((size_t)key >> 4) & 0xffffffff);
}

static uint32_t id_hash_function(const void *key)
{
    return (uint32_t)(key - (const void *)0);
}


#if 0
static const char *get_resource_appid(mrp_resource_t *res)
{
    mrp_attr_t attr;
    const char *appid;

    if (!mrp_resource_read_attribute(res, APPID_ATTRIDX, &attr) ||
        attr.type != mqi_string || !(appid = attr.value.string)  )
        appid = NULL;

    return appid;
}
#endif

static int32_t get_resource_sourceid(mrp_resource_t *res)
{
    mrp_attr_t attr;

    if (!mrp_resource_read_attribute(res, SRCID_ATTRIDX, &attr) ||
        attr.type != mqi_integer)
    {
        return 0;
    }

    return attr.value.integer;
}

static int32_t get_resource_sinkid(mrp_resource_t *res)
{
    mrp_attr_t attr;

    if (!mrp_resource_read_attribute(res, SINKID_ATTRIDX, &attr) ||
        attr.type != mqi_integer)
    {
        return 0;
    }

    return attr.value.integer;
}


static int32_t get_resource_connid(mrp_resource_t *res)
{
    mrp_attr_t attr;

    if (!mrp_resource_read_attribute(res, CONNID_ATTRIDX, &attr) ||
        attr.type != mqi_integer)
    {
        return 0;
    }

    return attr.value.integer;
}

static int32_t get_resource_connno(mrp_resource_t *res)
{
    mrp_attr_t attr;

    if (!mrp_resource_read_attribute(res, CONNNO_ATTRIDX, &attr) ||
        attr.type != mqi_integer)
    {
        return 0;
    }

    return attr.value.integer;
}

static int32_t get_resource_stamp(mrp_resource_t *res)
{
    mrp_attr_t attr;

    if (!mrp_resource_read_attribute(res, STAMP_ATTRIDX, &attr) ||
        attr.type != mqi_integer)
    {
        return 0;
    }

    return attr.value.integer;
}

static bool set_resource_source_and_sink(mrp_resource_t *res,
                                         mrp_resmgr_source_t *source,
                                         mrp_resmgr_sink_t *sink)
{
    const char *source_name;
    const char *sink_name;
    mrp_attr_t attrs[ATTR_MAX+1];

    memset(attrs, 0, sizeof(attrs));

    if (!mrp_resource_read_all_attributes(res, ATTR_MAX+1, attrs))
        return false;

    if (source && (source_name = mrp_resmgr_source_get_name(source)))
        attrs[SRCNAM_ATTRIDX].value.string = source_name;

    if (sink && (sink_name = mrp_resmgr_sink_get_name(sink)))
        attrs[SINKNAM_ATTRIDX].value.string = sink_name;

    if (mrp_resource_write_attributes(res, attrs) < 0)
        return false;

    return true;
}


static bool set_resource_stamp(mrp_resource_t *res, int32_t stamp)
{
    mrp_attr_t attrs[ATTR_MAX+1];

    memset(attrs, 0, sizeof(attrs));

    if (!mrp_resource_read_all_attributes(res, ATTR_MAX+1, attrs))
        return false;

    attrs[STAMP_ATTRIDX].value.integer = stamp;

    if (mrp_resource_write_attributes(res, attrs) < 0)
        return false;

    return true;
}

static bool set_resource_decision(mrp_resource_t *res, int32_t decision)
{
    const char *decision_name;
    mrp_attr_t attrs[ATTR_MAX+1];

    if (decision < 0 || decision >= DECISION_MAX)
        decision_name = "<error>";
    else
        decision_name = decision_names[decision];

    memset(attrs, 0, sizeof(attrs));

    if (!mrp_resource_read_all_attributes(res, ATTR_MAX+1, attrs))
        return false;

    attrs[DECISION_ATTRIDX].value.string = decision_name;

    if (mrp_resource_write_attributes(res, attrs) < 0)
        return false;

    return true;
}


static void resource_create(mrp_resmgr_backend_t *backend,
                            mrp_application_class_t *ac,
                            mrp_zone_t *zone,
                            mrp_resource_t *res)
{
    mrp_resmgr_t *resmgr;
    mrp_resmgr_resource_t *ar;
    uint32_t zoneid;
    uint32_t resid;
    resource_type_t *type;
    uint32_t srcid, sinkid;
    mrp_resmgr_source_t *src;
    mrp_resmgr_sink_t *sink;
    int32_t connid;
    int32_t connno;
    char name[256];

    MRP_UNUSED(ac);
    MRP_UNUSED(zone);

    MRP_ASSERT(backend && backend->resmgr && res, "invalid argument");

    resmgr = backend->resmgr;
    zoneid = mrp_zone_get_id(zone);
    resid  = mrp_resource_get_id(res);
    type   = find_resource_definition_by_id(backend, resid);
    srcid  = get_resource_sourceid(res);
    sinkid = get_resource_sinkid(res);
    src    = (srcid  > 0) ? mrp_resmgr_source_find_by_id(resmgr, srcid):NULL;
    sink   = (sinkid > 0) ? mrp_resmgr_sink_find_by_gam_id(resmgr,sinkid):NULL;
    connid = get_resource_connid(res);
    connno = get_resource_connno(res);

    if (!type)
        return;

    resource_print_name(src, connno, name, sizeof(name));


    if (!(ar = mrp_allocz(sizeof(mrp_resmgr_resource_t))))
        return;

    ar->name    = mrp_strdup(name);
    ar->backend = backend;
    ar->res     = res;
    ar->type    = type;
    ar->source  = src;
    ar->sink    = sink;
    ar->zoneid  = zoneid;
    ar->connid  = connid;
    ar->connno  = connno;

    mrp_list_init(&ar->source_link);

    if (!mrp_htbl_insert(backend->resources.by_pointer, res, ar)) {
        mrp_log_error("gam-resource-manager: can't add resource %s"
                      "to hash (by pointer)", ar->name);
        mrp_free(ar);
        return;
    }

    resource_register_by_id(backend, ar);

    set_resource_source_and_sink(ar->res, ar->source, ar->sink);
}

static void resource_destroy(mrp_resmgr_backend_t *backend,
                             mrp_zone_t *zone,
                             mrp_resource_t *res)
{
    mrp_resmgr_usecase_t *usecase;
    mrp_resmgr_resource_t *ar;
    mrp_htbl_t *hash;

    MRP_ASSERT(backend && backend->resmgr && zone && res,"invalid argument");

    if (!(hash = backend->resources.by_pointer) ||
        !(ar = mrp_htbl_remove(hash, res, false)))
    {
        mrp_debug("failed to destroy audio resource: can't find it");
        return;
    }

    mrp_debug("%s resource '%s' going to be destroyed",
              ar->type->name, ar->name);

    if (ar->connid > 0 && (hash = backend->resources.by_connid)) {
        if (ar != mrp_htbl_remove(hash, NULL + ar->connid, false)) {
            mrp_log_error("gam-resource-manager: confused with data "
                          "structures when attempting to remove "
                          "resource '%s' from ID hash", ar->name);
        }
    }

    mrp_list_delete(&ar->source_link);

    mrp_free((void *)ar->name);

    mrp_free(ar);

    usecase = mrp_resmgr_get_usecase(backend->resmgr);
    mrp_resmgr_usecase_update(usecase);
}

static bool resource_acquire(mrp_resmgr_backend_t *backend,
                             mrp_zone_t *zone,
                             mrp_resource_t *res)
{
    static int32_t stamp;

    mrp_resmgr_resource_t *ar;

    MRP_ASSERT(backend && zone && res, "invalid argument");

    if (!(ar = resource_lookup_by_pointer(backend, res))) {
        mrp_debug("failed to acquire audio resource: can't find it");
        return false;
    }

    if (!set_resource_stamp(ar->res, ++stamp)) {
        mrp_log_error("gam-resource-manager: failed to set 'stamp' "
                      "property for '%s' when acquiring", ar->name);
        return false;
    }

    return true;
}


static bool resource_release(mrp_resmgr_backend_t *backend,
                             mrp_zone_t *zone,
                             mrp_resource_t *res)
{
    mrp_resmgr_resource_t *ar;

    MRP_ASSERT(backend && zone && res, "invalid argument");

    if (!(ar = resource_lookup_by_pointer(backend, res))) {
        mrp_debug("failed to release audio resource: can't find it");
        return false;
    }

    if (!set_resource_stamp(ar->res, 0)) {
        mrp_log_error("gam-resource-manager: failed to set 'stamp' "
                      "property for '%s' when releasing", ar->name);
        return false;
    }

    return true;
}


static bool resource_register_by_id(mrp_resmgr_backend_t *backend,
                                    mrp_resmgr_resource_t *ar)
{
    if (ar->connid < 1)
        return false;

    if (!mrp_htbl_insert(backend->resources.by_connid, NULL + ar->connid, ar)) {
        mrp_log_error("gam-resource-manager: can't add resource '%s'"
                      "to hash (by connid)", ar->name);
        return false;
    }

    if (!mrp_resmgr_source_add_resource(ar->source, &ar->source_link)) {
        mrp_log_error("gam-resource-manager: can't add resource '%s'"
                      "to source", ar->name);
        return false;
    }

    return true;
}

static mrp_resmgr_resource_t *resource_lookup_by_pointer(mrp_resmgr_backend_t *backend,
                                                         mrp_resource_t *res)
{
    mrp_htbl_t *htbl;

    if (!backend || !(htbl = backend->resources.by_pointer) || !res)
        return NULL;

    return mrp_htbl_lookup(htbl, res);
}

static size_t resource_print_name(mrp_resmgr_source_t *src,
                                  uint32_t connno,
                                  char *name,
                                  size_t len)
{
    size_t ret;

    if (!src)
        ret = snprintf(name, len, "<invalid>");
    else if (connno < 2)
        ret = snprintf(name, len, "%s", mrp_resmgr_source_get_name(src));
    else {
        ret = snprintf(name, len, "%s%d", mrp_resmgr_source_get_name(src),
                       connno);
    }

    return ret;
}


static int decision_cb(void *key, void *object, void *user_data)
{
    mrp_resmgr_backend_t  *backend = (mrp_resmgr_backend_t *)user_data;
    mrp_resmgr_resource_t *ar = (mrp_resmgr_resource_t *)object;
    mrp_resmgr_t *resmgr;
    mrp_resmgr_usecase_t *usecase;
    bool sink_available, source_available;
    int32_t decision_new, state_new;
    mrp_attr_t attrs[ATTR_MAX+1];
    uint16_t src_id, sink_id;
    uint32_t connid;
    mrp_resmgr_source_t *src;
    bool need_update;
    char buf[512];

    MRP_UNUSED(key);
    MRP_UNUSED(user_data);

    MRP_ASSERT(ar && ar->backend == backend, "confused with data structures");

    if (!ar->sink || !ar->source || !ar->connno) {
        memset(attrs, 0, sizeof(attrs));
        need_update = false;

        if (mrp_resource_read_all_attributes(ar->res, ATTR_MAX+1, attrs)) {
            src_id = attrs[SRCID_ATTRIDX].value.integer;
            sink_id = attrs[SINKID_ATTRIDX].value.integer;
            connid = attrs[CONNID_ATTRIDX].value.integer;

            if (!ar->source && src_id > 0) {
                src = mrp_resmgr_source_find_by_id(backend->resmgr, src_id);

                if (src && mrp_resmgr_source_add_resource(src,&ar->source_link)) {
                    resource_print_name(src, ar->connno, buf, sizeof(buf));
                    mrp_free(ar->name);

                    ar->name = mrp_strdup(buf);
                    ar->source = src;

                    mrp_debug("update resource %s source to '%s'",
                              ar->name, mrp_resmgr_source_get_name(src));

                    need_update = true;
                }
            }

            if (!ar->sink && sink_id > 0) {
                ar->sink = mrp_resmgr_sink_find_by_gam_id(backend->resmgr,
                                                          sink_id);
                mrp_debug("update resource %s sink to '%s'",
                          ar->name, mrp_resmgr_sink_get_name(ar->sink));

                need_update = true;
            }

            if (!ar->connid && connid > 0) {
                ar->connid = connid;
                mrp_debug("update resource %s connid to %u",
                          ar->name, connid);

                need_update = true;
            }

            if (need_update) {
                resmgr = backend->resmgr;

                if ((usecase = mrp_resmgr_get_usecase(resmgr)))
                    mrp_resmgr_usecase_update(usecase);
            }
        }
    }

    source_available = mrp_resmgr_source_get_availability(ar->source);
    sink_available = mrp_resmgr_sink_get_availability(ar->sink);

    if (!source_available || !sink_available)
        decision_new = DECISION_DISCONNECTED; /* or teardown ? */
    else if (ar->connno != 0 || ar->connid < 1)
        decision_new = DECISION_DISCONNECTED;
    else
        decision_new = mrp_resmgr_source_make_decision(ar->source);

    if (decision_new < 0 || decision_new >= DECISION_MAX)
        decision_new = ar->decision.current;

    if (decision_new < 0 || decision_new >= DECISION_MAX)
        decision_new = 0;

    state_new = decision2state[decision_new];

    ar->decision.new = decision_new;
    ar->state.new = state_new;

    print_decision(ar, buf, sizeof(buf));
    mrp_debug("   %s", buf);

    return MRP_HTBL_ITER_MORE;
}

static void make_decisions(mrp_resmgr_backend_t *backend)
{
    mrp_resmgr_usecase_t *usecase;

    usecase = mrp_resmgr_get_usecase(backend->resmgr);
    mrp_resmgr_usecase_update(usecase);

    mrp_htbl_foreach(backend->resources.by_pointer, decision_cb, backend);
}

static int commit_cb(void *key, void *object, void *user_data)
{
    mrp_resmgr_backend_t  *backend = (mrp_resmgr_backend_t *)user_data;
    mrp_resmgr_resource_t *ar = (mrp_resmgr_resource_t *)object;
    char buf[256];

    MRP_UNUSED(key);
    MRP_UNUSED(user_data);

    MRP_ASSERT(ar && ar->backend == backend, "confused with data structures");

    if (ar->source) {
        print_commit(ar, buf, sizeof(buf));
        mrp_debug("   %s", buf);

        ar->decision.current = ar->decision.new;
        ar->state.current = ar->state.new;

        set_resource_decision(ar->res, ar->decision.current);
    }

    return MRP_HTBL_ITER_MORE;
}

static void commit_decisions(mrp_resmgr_backend_t *backend)
{
    mrp_resmgr_usecase_t *usecase;

    mrp_htbl_foreach(backend->resources.by_pointer, commit_cb, backend);

    usecase = mrp_resmgr_get_usecase(backend->resmgr);
    mrp_resmgr_usecase_update(usecase);
}


static size_t print_decision(mrp_resmgr_resource_t *ar, char *buf, size_t len)
{
    char name [256];
    char decision[256];
    char state[256];

    snprintf(name, sizeof(name), "%s:", ar->name);

    if (ar->decision.new == ar->decision.current) {
        snprintf(decision, sizeof(decision), "%s (no change)",
                 decision_names[ar->decision.new]);
    }
    else {
        snprintf(decision, sizeof(decision), "%s => %s",
                 decision_names[ar->decision.current],
                 decision_names[ar->decision.new]);
    }

    if (ar->state.new == ar->state.current)
        state[0] = 0;
    else {
        snprintf(state, sizeof(state), "%s => %s",
                 state_names[ar->state.current],
                 state_names[ar->state.new]);
    }

    return snprintf(buf, len, "%-24s %-28s %s", name, decision, state);
}


static size_t print_commit(mrp_resmgr_resource_t *ar, char *buf, size_t len)
{
    const char *decision;
    const char *state;
    char name[256];

    snprintf(name, sizeof(name), "%s:", ar->name);

    decision = decision_names[ar->decision.new];
    state = state_names[ar->state.new];

    return snprintf(buf, len, "%-24s %-12s %s", name, decision, state);
}


static void backend_notify(mrp_resource_event_t event,
                           mrp_zone_t *zone,
                           mrp_application_class_t *ac,
                           mrp_resource_t *res,
                           void *userdata)
{
    mrp_resmgr_backend_t *backend = (mrp_resmgr_backend_t *)userdata;
    const char *zonename = mrp_zone_get_name(zone);

    MRP_ASSERT(zone && ac && res && backend, "invalid argument");

    switch (event) {

    case MRP_RESOURCE_EVENT_CREATED:
        mrp_debug("audio resource in zone '%s' created", zonename);
        resource_create(backend, ac, zone, res);
        break;

    case MRP_RESOURCE_EVENT_DESTROYED:
        mrp_debug("audio resource in zone '%s' destroyed", zonename);
        resource_destroy(backend, zone, res);
        break;

    case MRP_RESOURCE_EVENT_ACQUIRE:
        mrp_debug("audio resource in zone '%s' is acquiring", zonename);
        resource_acquire(backend, zone, res);
        break;

    case MRP_RESOURCE_EVENT_RELEASE:
        mrp_debug("audio resource in zone '%s' is released", zonename);
        resource_release(backend, zone, res);
        break;

    default:
        mrp_log_error("gam-resource-manager: invalid event %d at audio "
                      "notification (zone '%s')", event, zonename);
        break;
    }
}

static void backend_init(mrp_zone_t *zone, void *userdata)
{
    mrp_resmgr_backend_t *backend = (mrp_resmgr_backend_t *)userdata;
    // uint32_t zoneid;
    const char *zonename;

    MRP_ASSERT(zone && backend && backend->resmgr, "invalid argument");

    // zoneid   = mrp_zone_get_id(zone);
    zonename = mrp_zone_get_name(zone);

    if (!zonename)
        zonename = "<unknown>";

    mrp_debug("audio init in zone '%s'", zonename);

    make_decisions(backend);
}

static bool backend_allocate(mrp_zone_t *zone,
                            mrp_resource_t *res,
                            void *userdata)
{
    mrp_resmgr_backend_t *backend = (mrp_resmgr_backend_t *)userdata;
    // uint32_t zoneid;
    const char *zonename;
    mrp_resmgr_resource_t *ar;
    bool allocated;

    MRP_ASSERT(zone && res && backend && backend->resmgr, "invalid argument");

    // zoneid = mrp_zone_get_id(zone);

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";

    if ((ar = resource_lookup_by_pointer(backend, res))) {
        allocated = (ar->state.new == STATE_PLAY);

        mrp_debug("%s allocation for '%s' in zone '%s' %s",
                  ar->type->name, ar->name, zonename,
                  allocated ? "succeeded":"failed");

        return allocated;
    }

    mrp_log_error("gam-resource-manager: attempt to allocate untracked "
                  "resource in zone '%s'", zonename);

    return FALSE;
}

static void backend_free(mrp_zone_t *zone, mrp_resource_t *res, void *userdata)
{
    mrp_resmgr_backend_t *backend = (mrp_resmgr_backend_t *)userdata;
    const char *zonename;
    mrp_resmgr_resource_t *ar;

    MRP_ASSERT(zone && res && backend, "invalid argument");

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";

    if ((ar = resource_lookup_by_pointer(backend, res))) {
        ar->decision.new = DECISION_DISCONNECTED;
        ar->state.new = decision2state[ar->decision.new];

        mrp_debug("free %s of '%s' in zone '%s'",
                  ar->type->name, ar->name, zonename);

        return;
    }

    mrp_log_error("gam-resource-manager: attempt to free untracked "
                  "resource in zone '%s'", zonename);
}

static bool backend_advice(mrp_zone_t *zone,mrp_resource_t *res,void *userdata)
{
#if 1
    MRP_UNUSED(zone);
    MRP_UNUSED(res);
    MRP_UNUSED(userdata);
#else
    mrp_resmgr_backend_t *backend = (mrp_resmgr_backend_t *)userdata;
    const char *zonename;
    const char *appid;

    MRP_ASSERT(zone && res && backend, "invalid argument");

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";
    if (!(appid = get_resource_appid(res)))
        appid = "<unknown>";

    mrp_debug("audio advice for '%s' in zone '%s'", appid, zonename);
#endif

    return TRUE;
}

static void backend_commit(mrp_zone_t *zone, void *userdata)
{
    mrp_resmgr_backend_t *backend = (mrp_resmgr_backend_t *)userdata;
    const char *zonename;
    // uint32_t zoneid;

    MRP_ASSERT(zone && backend && backend->resmgr, "invalid argument");

    // zoneid  = mrp_zone_get_id(zone);

    if (!(zonename = mrp_zone_get_name(zone)))
        zonename = "<unknown>";

    mrp_debug("audio commit in zone '%s'", zonename);

    commit_decisions(backend);
}
