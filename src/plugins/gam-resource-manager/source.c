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

#include "source.h"
#include "usecase.h"
#include "backend.h"

#define COLUMN_MAX 4

typedef struct gam_source_s   gam_source_t;
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


struct mrp_resmgr_sources_s {
    mrp_resmgr_t *resmgr;
    struct {
        mrp_htbl_t *by_name;
        mrp_htbl_t *by_id;
    } lookup;
    mrp_decision_conf_t *decision_conf;
    table_t state_table;
};

struct gam_source_s {
    const char *name;
    uint16_t id;
};


struct mrp_resmgr_source_s {
    mrp_resmgr_sources_t *sources;
    gam_source_t gam_source;
    mrp_list_hook_t resources;
    mrp_decision_tree_t *decision_tree;
    bool available;
};


static bool register_gam_id(mrp_resmgr_source_t *, uint16_t);

static mrp_decision_conf_t *load_decision_conf(const char *);
static bool decision_values_match(mrp_decision_conf_t *);

static void source_free(void *, void *);
static int id_hash_compare(const void *, const void *);
static uint32_t id_hash_function(const void *);

static bool source_status_changed_cb(mrp_resmgr_t *);
static mqi_handle_t get_table_handle(mrp_resmgr_sources_t *);


mrp_resmgr_sources_t *mrp_resmgr_sources_create(mrp_resmgr_t *resmgr)
{
    mrp_resmgr_config_t *config;
    mrp_resmgr_sources_t *sources;
    mrp_htbl_config_t ncfg, icfg;
    char stem[1024];

    MRP_ASSERT(resmgr, "invalid argument");

    config = mrp_resmgr_get_config(resmgr);
    snprintf(stem, sizeof(stem), "%s/%s", config->confdir, config->confnams);

    memset(&ncfg, 0, sizeof(ncfg));
    ncfg.nentry = MRP_RESMGR_SOURCE_MAX;
    ncfg.comp = mrp_string_comp;
    ncfg.hash = mrp_string_hash;
    ncfg.free = source_free;
    ncfg.nbucket = MRP_RESMGR_SOURCE_BUCKETS;

    memset(&icfg, 0, sizeof(icfg));
    icfg.nentry = MRP_RESMGR_SOURCE_MAX;
    icfg.comp = id_hash_compare;
    icfg.hash = id_hash_function;
    icfg.free = NULL;
    icfg.nbucket = MRP_RESMGR_SOURCE_BUCKETS;

    if ((sources = mrp_allocz(sizeof(mrp_resmgr_sources_t)))) {
        sources->resmgr = resmgr;
        sources->lookup.by_name = mrp_htbl_create(&ncfg);
        sources->lookup.by_id = mrp_htbl_create(&icfg);
        sources->decision_conf = load_decision_conf(stem);
        sources->state_table.handle = MQI_HANDLE_INVALID;

        mrp_resmgr_register_dependency(resmgr, MRP_RESMGR_SOURCE_STATE_TABLE,
                                       source_status_changed_cb);
    }

    return sources;
}


void mrp_resmgr_sources_destroy(mrp_resmgr_sources_t *sources)
{
    if (sources) {
        mrp_decision_conf_destroy(sources->decision_conf);

        mrp_htbl_destroy(sources->lookup.by_id, false);
        mrp_htbl_destroy(sources->lookup.by_name, true);

        mrp_free(sources);
    }
}

mrp_decision_conf_t *mrp_resmgr_sources_get_decision_conf(mrp_resmgr_t *resmgr)
{
    mrp_resmgr_sources_t *sources;

    if (!resmgr || !(sources = mrp_resmgr_get_sources(resmgr)))
        return NULL;

    return sources->decision_conf;
}

mrp_resmgr_source_t *mrp_resmgr_source_add(mrp_resmgr_t *resmgr,
                                           const char *gam_name,
                                           uint16_t gam_id)
{
#define ATTR_MAX   MRP_RESMGR_USECASE_SIZE_MAX

    mrp_resmgr_config_t *config;
    mrp_resmgr_sources_t *sources;
    mrp_resmgr_usecase_t *usecase;
    mrp_resmgr_source_t *src;
    char *gam_name_dup;
    mrp_decision_conf_t *dc;
    ssize_t nattr, i;
    const char *attrs[ATTR_MAX];
    ssize_t offs;
    char stem[1024];

    MRP_ASSERT(resmgr && gam_name, "invalid argument");

    config = mrp_resmgr_get_config(resmgr);
    sources = mrp_resmgr_get_sources(resmgr);
    usecase = mrp_resmgr_get_usecase(resmgr);

    MRP_ASSERT(config && sources && usecase, "internal error");

    if (!(src = mrp_resmgr_source_find_by_name(resmgr, gam_name))) {
        snprintf(stem, sizeof(stem), "%s/%s-%s-%d", config->confdir,
                 config->prefix, gam_name, config->max_active);

        dc = sources->decision_conf;
        nattr = mrp_decision_attr_list(dc, attrs, ATTR_MAX);

        if (dc && nattr > 0 &&
            (src = mrp_allocz(sizeof(mrp_resmgr_source_t))) &&
            (gam_name_dup = mrp_strdup(gam_name)))
        {
            src->sources = sources;
            src->gam_source.name = gam_name_dup;
            src->gam_source.id = gam_id;
            mrp_list_init(&src->resources);

            if (!mrp_htbl_insert(sources->lookup.by_name, gam_name_dup, src)) {
                mrp_log_error("gam-resource-manager: attempt to add source "
                              "'%s' multiple times", gam_name_dup);
                goto failed;
            }

            for (i = 0;  i < nattr;  i++) {
                offs = mrp_resmgr_usecase_add_attribute(usecase,src,attrs[i]);

                if (offs < 0) {
                    mrp_log_error("gam-resource-manager: failed to add "
                                  "attribute '%s' to source '%s'",
                                  attrs[i], gam_name);
                }
                else {
                    if (!mrp_decision_set_attr_offset(dc, attrs[i], offs)) {
                        mrp_log_error("gam-resource-manager: failed to set "
                                      "offset for attribute '%s' in source "
                                      "'%s'", attrs[i], gam_name);
                    }
                }
            }

            src->decision_tree = mrp_decision_tree_create_from_file(dc, stem);

            if (!src->decision_tree) {
                mrp_log_error("gam-resource-manager: source '%s' "
                              "is nonfunctional", gam_name);
            }
        }
    }

    if (src) {
        register_gam_id(src, gam_id);
    }

    return src;

 failed:
    if (src)
        source_free((void *)src->gam_source.name, (void *)src);
    return NULL;

#undef ATTR_MAX
}

mrp_resmgr_source_t *mrp_resmgr_source_find_by_name(mrp_resmgr_t *resmgr,
                                                    const char *gam_name)
{
    mrp_resmgr_sources_t *sources;

    MRP_ASSERT(resmgr && gam_name, "invalid argument");

    if (!(sources = mrp_resmgr_get_sources(resmgr)) ||
        !(sources->lookup.by_name))
    {
        return NULL;
    }

    return mrp_htbl_lookup(sources->lookup.by_name, (void *)gam_name);
}


mrp_resmgr_source_t *mrp_resmgr_source_find_by_id(mrp_resmgr_t *resmgr,
                                                  uint16_t gam_id)
{
    mrp_resmgr_sources_t *sources;

    MRP_ASSERT(resmgr, "invalid argument");

    if (!(sources = mrp_resmgr_get_sources(resmgr)) ||
        !(sources->lookup.by_id))
    {
        return NULL;
    }

    return mrp_htbl_lookup(sources->lookup.by_id, NULL + gam_id);
}




const char *mrp_resmgr_source_get_name(mrp_resmgr_source_t *src)
{
    if (!src)
        return "<invalid>";

    return src->gam_source.name;
}

bool mrp_resmgr_source_get_availability(mrp_resmgr_source_t *src)
{
    if (!src)
        return false;

    return src->available;
}


mrp_resmgr_resource_t *mrp_resmgr_source_get_resource(mrp_resmgr_source_t *src,
                                                      uint32_t connno)
{
    mrp_list_hook_t *entry, *n;
    mrp_resmgr_resource_t *ar;

    MRP_ASSERT(src, "invalid argument");

    mrp_list_foreach(&src->resources, entry, n) {
        ar = mrp_resmgr_backend_resource_list_entry(entry);
        if (connno == mrp_resmgr_backend_get_resource_connno(ar))
            return ar;
    }

    return NULL;
}

bool mrp_resmgr_source_add_resource(mrp_resmgr_source_t *src,
                                    mrp_list_hook_t *ar_source_link)
{
    mrp_resmgr_resource_t *ar, *ar2;
    uint32_t connno, connno2;
    mrp_list_hook_t *entry, *n;
    mrp_list_hook_t *insert_after;
    bool success;

    MRP_ASSERT(src && ar_source_link, "invalid argument");

    ar = mrp_resmgr_backend_resource_list_entry(ar_source_link);
    connno = mrp_resmgr_backend_get_resource_connno(ar);

    success = true;
    insert_after = &src->resources;

    mrp_list_foreach(&src->resources, entry, n) {
        ar2 = mrp_resmgr_backend_resource_list_entry(entry);
        connno2 = mrp_resmgr_backend_get_resource_connno(ar2);

        if (connno2 == connno) {
            success = false;
            break;
        }

        if (connno2 < connno)
            insert_after = entry;
    }

    if (success)
        mrp_list_insert_after(insert_after, ar_source_link);

    return success;
}

int32_t mrp_resmgr_source_make_decision(mrp_resmgr_source_t *src)
{
    mrp_resmgr_t *resmgr;
    mrp_resmgr_sources_t *sources;
    mrp_resmgr_usecase_t *usecase;
    void *input;
    mrp_decision_value_type_t type;
    mrp_decision_value_t *value;
    int32_t decision;

    MRP_ASSERT(src && src->sources && src->sources->resmgr,"invalid argument");

    sources = src->sources;
    resmgr  = sources->resmgr;
    usecase = mrp_resmgr_get_usecase(resmgr);
    input   = mrp_resmgr_usecase_get_decision_input(usecase);

    if (!mrp_decision_make(src->decision_tree, input, &type, &value))
        decision = -1;
    else
        decision = value->integer;

    return decision;
}

static bool register_gam_id(mrp_resmgr_source_t *src, uint16_t gam_id)
{
    mrp_resmgr_sources_t *sources;

    if (!(sources = src->sources))
        return false;

    if (!gam_id || gam_id == src->gam_source.id)
        return true;

    if (src->gam_source.id) {
        mrp_log_error("gam-resource-manager: attempt to reset "
                      "gam ID of '%s'", src->gam_source.name);
        return false;
    }

    if (!mrp_htbl_insert(sources->lookup.by_id,  NULL+gam_id, src)) {
        mrp_log_error("gam-resource-manager: attempt to add source "
                      "%d multiple times", gam_id);
        return false;
    }

    src->gam_source.id = gam_id;

    mrp_debug("assign id %d to source '%s'", gam_id, src->gam_source.name);

    return true;
}

static mrp_decision_conf_t *load_decision_conf(const char *stem)
{
    mrp_decision_conf_t *conf;

    if (!(conf = mrp_decision_conf_create_from_file(stem))) {
        mrp_log_error("gam-resource-manager: can't load "
                      "decision conf file %s.names", stem);
    }
    else if (!decision_values_match(conf)) {
        mrp_log_error("gam-resource-manager: decision values in conf file "
                      "%s.name do not match their builtin counterpart", stem);

        mrp_decision_conf_destroy(conf);
        conf = NULL;
    }

    return conf;
}

static bool decision_values_match(mrp_decision_conf_t *conf)
{
    const char **names = mrp_resmgr_backend_get_decision_names();
    int32_t i;

    if (!conf)
        return false;

    for (i = 0;  names[i];  i++) {
        if (strcmp(names[i], mrp_decision_name(conf, i)))
            return false;
    }

    return (i > 0 && i == mrp_decision_value_max(conf));
}

static void source_free(void *key, void *object)
{
    mrp_resmgr_source_t *src = (mrp_resmgr_source_t *)object;

    MRP_ASSERT(key && object, "internal error");
    MRP_ASSERT(src->gam_source.name == (const char *)key, "corrupt data");
    MRP_ASSERT(mrp_list_empty(&src->resources), "resource list not empty");

    mrp_decision_tree_destroy(src->decision_tree);

    mrp_free((void *)src->gam_source.name);

    mrp_free(src);
}

static int id_hash_compare(const void *key1, const void *key2)
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


static bool source_status_changed_cb(mrp_resmgr_t *resmgr)
{
    mrp_resmgr_sources_t *sources;
    mrp_resmgr_source_t *src;
    mqi_handle_t h;
    int i, n;
    row_t rows[MRP_RESMGR_SOURCE_MAX];
    row_t *r;
    int change_count;

    MRP_ASSERT(resmgr, "invalid argument");

    printf("### %s() called\n", __FUNCTION__);

    if (!(sources = mrp_resmgr_get_sources(resmgr)) ||
        !sources->lookup.by_name)
        return false;

    if ((h = get_table_handle(sources)) == MQI_HANDLE_INVALID) {
        mrp_log_error("gam-resource-manager: can't update status changes: "
                      "database error");
        return false;
    }

    memset(rows, 0, sizeof(rows));

    if ((n = MQI_SELECT(sources->state_table.cols, h, MQI_ALL, rows)) < 0) {
        mrp_log_error("gam-resource-manager: select on table '%s' failed: %s",
                      MRP_RESMGR_SOURCE_STATE_TABLE, strerror(errno));
        return false;
    }

    if (n == 0)
        return false;

    for (change_count = i = 0;  i < n;  i++) {
        r = rows + i;

        if (!r->visible)
            continue;

        if ((src = mrp_htbl_lookup(sources->lookup.by_name, (void *)r->name))){
            register_gam_id(src, r->id);

            if (( r->available && !src->available) ||
                (!r->available &&  src->available)  )
            {
                src->available = r->available;
                change_count++;

                mrp_debug("%s become %savailable", src->gam_source.name,
                          src->available ? "":"un");
            }
        }
    }

    return change_count > 0;
}

static mqi_handle_t get_table_handle(mrp_resmgr_sources_t *sources)
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

    if (sources->state_table.handle == MQI_HANDLE_INVALID) {
        h = mqi_get_table_handle(MRP_RESMGR_SOURCE_STATE_TABLE);

        if (h != MQI_HANDLE_INVALID) {
            sources->state_table.handle = h;

            for (i = 0;  i < COLUMN_MAX;  i++) {
                def = col_defs + i;
                desc = sources->state_table.cols + i;

                if ((desc->cindex = mqi_get_column_index(h, def->name)) < 0) {
                    mrp_log_error("gam-resource-manager: can't find column "
                                  "'%s' in table '%s'", def->name,
                                  MRP_RESMGR_SOURCE_STATE_TABLE);
                    sources->state_table.handle = MQI_HANDLE_INVALID;
                    break;
                }

                desc->offset = def->offset;
            }

            desc = sources->state_table.cols + i;
            desc->cindex = -1;
            desc->offset = -1;
        }

    }

    return sources->state_table.handle;

#undef COLUMN
}
