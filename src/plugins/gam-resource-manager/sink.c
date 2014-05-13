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

#include "sink.h"
#include "source.h"
#include "c5-decision-tree.h"

#define COLUMN_MAX 4

typedef struct gam_sink_s     gam_sink_t;
typedef struct table_s        table_t;
typedef struct row_s          row_t;
typedef struct colum_def_s    column_def_t;

struct table_s {
    mqi_handle_t handle;
    mqi_column_desc_t cols[COLUMN_MAX+1];
};

struct row_s {
    int32_t id;
    const char *name;
    int32_t available;
    int32_t visible;
};

struct colum_def_s {
    char *name;
    mqi_data_type_t type;
    int offset;
};

struct mrp_resmgr_sinks_s {
    mrp_resmgr_t *resmgr;
    struct {
        mrp_htbl_t *by_name;
        mrp_htbl_t *by_id;
    } lookup;
    table_t state_table;
};

struct gam_sink_s {
    const char *name;
    uint16_t id;
};


struct mrp_resmgr_sink_s {
    mrp_resmgr_sinks_t *sinks;
    gam_sink_t gam_sink;
    mrp_htbl_t *decision_ids;
    bool available;
};

static bool register_gam_id(mrp_resmgr_sink_t *, uint16_t);

static void sink_free(void *, void *);

static int hash_compare(const void *, const void *);
static uint32_t id_hash_function(const void *);
static uint32_t ptr_hash_function(const void *);

static bool sink_status_changed_cb(mrp_resmgr_t *);
static mqi_handle_t get_table_handle(mrp_resmgr_sinks_t *);



mrp_resmgr_sinks_t *mrp_resmgr_sinks_create(mrp_resmgr_t *resmgr)
{
    mrp_resmgr_sinks_t *sinks;
    mrp_htbl_config_t ncfg, icfg;

    MRP_ASSERT(resmgr, "invalid argument");

    memset(&ncfg, 0, sizeof(ncfg));
    ncfg.nentry = MRP_RESMGR_SINK_MAX;
    ncfg.comp = mrp_string_comp;
    ncfg.hash = mrp_string_hash;
    ncfg.free = sink_free;
    ncfg.nbucket = MRP_RESMGR_SINK_BUCKETS;

    memset(&icfg, 0, sizeof(icfg));
    icfg.nentry = MRP_RESMGR_SINK_MAX;
    icfg.comp = hash_compare;
    icfg.hash = id_hash_function;
    icfg.free = NULL;
    icfg.nbucket = MRP_RESMGR_SINK_BUCKETS;

    if ((sinks = mrp_allocz(sizeof(mrp_resmgr_sinks_t)))) {
        sinks->resmgr = resmgr;
        sinks->lookup.by_name = mrp_htbl_create(&ncfg);
        sinks->lookup.by_id = mrp_htbl_create(&icfg);
        sinks->state_table.handle = MQI_HANDLE_INVALID;

        mrp_resmgr_register_dependency(resmgr, MRP_RESMGR_SINK_STATE_TABLE,
                                       sink_status_changed_cb);
    }

    return sinks;
}

void mrp_resmgr_sinks_destroy(mrp_resmgr_sinks_t *sinks)
{
    if (sinks) {
        mrp_htbl_destroy(sinks->lookup.by_id, false);
        mrp_htbl_destroy(sinks->lookup.by_name, true);
        mrp_free(sinks);
    }
}

mrp_resmgr_sink_t *mrp_resmgr_sink_add(mrp_resmgr_t *resmgr,
                                       const char *name,
                                       int32_t id,
                                       mrp_resmgr_source_t *source)
{
    mrp_resmgr_config_t *config;
    mrp_resmgr_sinks_t *sinks;
    mrp_resmgr_sink_t *sink;
    char *name_dup;
    mrp_htbl_config_t cfg;

    MRP_ASSERT(resmgr && name, "invalid argument");

    config = mrp_resmgr_get_config(resmgr);
    sinks = mrp_resmgr_get_sinks(resmgr);

    MRP_ASSERT(config && sinks, "internal error");

    if (!(sink = mrp_resmgr_sink_find_by_name(resmgr, name))) {

        /* new sink */
        memset(&cfg, 0, sizeof(cfg));
        cfg.nentry = MRP_RESMGR_SOURCE_MAX;
        cfg.comp = hash_compare;
        cfg.hash = ptr_hash_function;
        cfg.free = NULL;
        cfg.nbucket = MRP_RESMGR_SOURCE_BUCKETS;

        if ((sink = mrp_allocz(sizeof(mrp_resmgr_sink_t))) &&
            (name_dup = mrp_strdup(name)))
        {
            sink->sinks = sinks;
            sink->gam_sink.name = name_dup;
            sink->decision_ids = mrp_htbl_create(&cfg);

            if (!mrp_htbl_insert(sinks->lookup.by_name, name_dup, sink)) {
                mrp_log_error("gam-resource-manager: attempt to add sink "
                              "'%s' multiple times", name_dup);

                sink_free((void *)sink->gam_sink.name, (void *)sink);
                sink = NULL;
            }
        }
    }

    if (sink) {
        if (!source) {
            /* id is a gam sink ID */
            register_gam_id(sink, id);
        }
        else {
            /* id is the enumeration of the source in the decision conf */
            if (!(mrp_htbl_insert(sink->decision_ids, source, NULL + (id+1)))){
                mrp_log_error("gam-resource-manager: attempt to add id %d "
                              "multiple time for source '%s'",
                              id, mrp_resmgr_source_get_name(source));
            }
        }
    }

    return sink;
}

mrp_resmgr_sink_t *mrp_resmgr_sink_find_by_name(mrp_resmgr_t *resmgr,
                                                const char *name)
{
    mrp_resmgr_sinks_t *sinks;

    MRP_ASSERT(resmgr && name, "invalid argument");

    if (!(sinks = mrp_resmgr_get_sinks(resmgr)) || !(sinks->lookup.by_name))
        return NULL;

    return mrp_htbl_lookup(sinks->lookup.by_name, (void *)name);
}


mrp_resmgr_sink_t *mrp_resmgr_sink_find_by_gam_id(mrp_resmgr_t *resmgr,
                                                  uint16_t gam_id)
{
    mrp_resmgr_sinks_t *sinks;

    MRP_ASSERT(resmgr, "invalid argument");

    if (!(sinks = mrp_resmgr_get_sinks(resmgr)) || !(sinks->lookup.by_id))
        return NULL;

    return mrp_htbl_lookup(sinks->lookup.by_id, NULL + gam_id);
}


const char *mrp_resmgr_sink_get_name(mrp_resmgr_sink_t *sink)
{
    MRP_ASSERT(sink, "invalid argument");

    return sink->gam_sink.name;
}

bool mrp_resmgr_sink_get_availability(mrp_resmgr_sink_t *sink)
{
    if (!sink)
        return false;

    return sink->available;
}


int32_t mrp_resmgr_sink_get_decision_id(mrp_resmgr_sink_t *sink,
                                        mrp_resmgr_source_t *source)
{
    void *void_id;

    if (!sink || !source)
        return -1;

    if (!(void_id = mrp_htbl_lookup(sink->decision_ids, source)))
        return -1;

    return (void_id - NULL) - 1;
}

static bool register_gam_id(mrp_resmgr_sink_t *sink, uint16_t gam_id)
{
    mrp_resmgr_sinks_t *sinks;

    if (!(sinks = sink->sinks))
        return false;

    if (!gam_id || gam_id == sink->gam_sink.id)
        return true;

    if (sink->gam_sink.id) {
        mrp_log_error("gam-resource-manager: attempt to reset "
                      "gam ID of '%s'", sink->gam_sink.name);
        return false;
    }

    if (!mrp_htbl_insert(sinks->lookup.by_id, NULL+gam_id, sink)) {
        mrp_log_error("gam-resource-manager: attempt to add "
                      "sink %d multiple times", gam_id);
        return false;
    }

    sink->gam_sink.id = gam_id;

    mrp_debug("assign id %d to sink '%s'", gam_id, sink->gam_sink.name);


    return true;
}

static void sink_free(void *key, void *object)
{
    mrp_resmgr_sink_t *sink = (mrp_resmgr_sink_t *)object;

    MRP_ASSERT(key && object, "internal error");
    MRP_ASSERT(sink->gam_sink.name == (const char *)key, "corrupt data");

    mrp_htbl_destroy(sink->decision_ids, false);

    mrp_free((void *)sink->gam_sink.name);

    free(sink);
}

static int hash_compare(const void *key1, const void *key2)
{
    if (key1 < key2)
        return -1;
    if (key1 > key2)
        return 1;
    return 0;
}

static uint32_t id_hash_function(const void *key)
{
    return (uint32_t)(key - (const void *)0);
}

static uint32_t ptr_hash_function(const void *key)
{
    return (uint32_t)(((size_t)key >> 4) & 0xffffffff);
}

static bool sink_status_changed_cb(mrp_resmgr_t *resmgr)
{
    mrp_resmgr_sinks_t *sinks;
    mrp_resmgr_sink_t *sink;
    mqi_handle_t h;
    int i, n;
    row_t rows[MRP_RESMGR_SOURCE_MAX];
    row_t *r;
    int change_count;

    MRP_ASSERT(resmgr, "invalid argument");

    printf("### %s() called\n", __FUNCTION__);

    if (!(sinks = mrp_resmgr_get_sinks(resmgr)) ||
        !sinks->lookup.by_name)
        return false;

    if ((h = get_table_handle(sinks)) == MQI_HANDLE_INVALID) {
        mrp_log_error("gam-resource-manager: can't update status changes: "
                      "database error");
        return false;
    }

    memset(rows, 0, sizeof(rows));

    if ((n = MQI_SELECT(sinks->state_table.cols, h, MQI_ALL, rows)) < 0) {
        mrp_log_error("gam-resource-manager: select on table '%s' failed: %s",
                      MRP_RESMGR_SINK_STATE_TABLE, strerror(errno));
        return false;
    }

    if (n == 0)
        return false;

    for (change_count = i = 0;  i < n;  i++) {
        r = rows + i;

        if (!r->visible)
            continue;

        if ((sink = mrp_htbl_lookup(sinks->lookup.by_name, (void *)r->name))) {
            register_gam_id(sink, r->id);

            if (( r->available && !sink->available) ||
                (!r->available &&  sink->available)  )
            {
                sink->available = r->available;
                change_count++;

                mrp_debug("%s become %savailable", sink->gam_sink.name,
                          sink->available ? "":"un");
            }
        }
    }

    return change_count > 0;
}


static mqi_handle_t get_table_handle(mrp_resmgr_sinks_t *sinks)
{
#define COLUMN(_n, _t)  { # _n, mqi_ ## _t, MQI_OFFSET(row_t, _n) }

    static column_def_t col_defs[COLUMN_MAX] = {
        COLUMN( id       , integer ),
        COLUMN( name     , string  ),
        COLUMN( available, integer ),
        COLUMN( visible  , integer ),
    };

    mqi_handle_t h;
    column_def_t *def;
    mqi_column_desc_t *desc;
    int i;

    if (sinks->state_table.handle == MQI_HANDLE_INVALID) {
        h = mqi_get_table_handle(MRP_RESMGR_SINK_STATE_TABLE);

        if (h != MQI_HANDLE_INVALID) {
            sinks->state_table.handle = h;

            for (i = 0;  i < COLUMN_MAX;  i++) {
                def = col_defs + i;
                desc = sinks->state_table.cols + i;

                if ((desc->cindex = mqi_get_column_index(h, def->name)) < 0) {
                    mrp_log_error("gam-resource-manager: can't find column "
                                  "'%s' in table '%s'", def->name,
                                  MRP_RESMGR_SINK_STATE_TABLE);
                    sinks->state_table.handle = MQI_HANDLE_INVALID;
                    break;
                }

                desc->offset = def->offset;
            }

            desc = sinks->state_table.cols + i;
            desc->cindex = -1;
            desc->offset = -1;
        }

    }

    return sinks->state_table.handle;

#undef COLUMN
}
