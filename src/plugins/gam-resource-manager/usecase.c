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
#include <ctype.h>
#include <errno.h>

#include <murphy/common.h>

#include "usecase.h"
#include "source.h"
#include "sink.h"
#include "backend.h"
#include "c5-decision-tree.h"

typedef enum   attr_type_e        attr_type_t;

typedef struct update_s           update_t;
typedef struct attr_def_s         attr_def_t;
typedef struct entry_iterator_s   entry_iterator_t;
typedef struct entry_descr_s      entry_descr_t;

struct mrp_resmgr_usecase_s {
    mrp_resmgr_t *resmgr;
    size_t size;
    int32_t *vector;
    uint32_t nentry;
    mrp_htbl_t *entries;
    int nupdate;
    update_t *updates;
    int nstamp;
    uint32_t *stamps;
};


struct update_s {
    mrp_resmgr_source_t *source;
    uint32_t connno;
    int32_t attr_idx;
    uint32_t vector_idx;
    bool stamp;
};

enum attr_type_e {
    REGULAR = 0,
    STAMP,
    ROUTE
};

struct attr_def_s {
    const char *suffix;
    size_t len;
    const char *name;
    int32_t idx;
    attr_type_t type;
};

struct entry_descr_s {
    const char *name;
    int update_idx;
};

struct entry_iterator_s {
    int maxentry;
    int nentry;
    entry_descr_t descs[0];
};


#define ATTR(_s,_n,_id,_b)    { _s,sizeof(_s)-1,_n,_id,_b }
#define ATTR_END              { NULL, 0, NULL, 0, false }

static attr_def_t attr_defs[] = {
    ATTR( "Route", "sink_id", MRP_RESMGR_RESOURCE_FIELD_DECISION_ID, ROUTE   ),
    ATTR( "Stamp", "stamp"  ,                    0                 , STAMP   ),
    ATTR( "State", "state"  , MRP_RESMGR_RESOURCE_FIELD_STATE      , REGULAR ),
    ATTR_END
};

#undef ATTR_END
#undef ATTR



static void hash_free(void *, void *);

bool parse_name(const char *, const char **, uint32_t *, attr_def_t **,
                char *, int);

static uint32_t add_update_to_usecase(mrp_resmgr_usecase_t *, int32_t,
                                      uint32_t, attr_type_t);
static uint32_t add_entry_to_vector(mrp_resmgr_usecase_t *);
static void add_stamp_to_usecase(mrp_resmgr_usecase_t *, uint32_t);
static void register_sink_ids(mrp_resmgr_usecase_t *, const char *,
                              mrp_resmgr_source_t *);

static void normalise_stamps(int32_t *vector, int nstamp, uint32_t *stamps);


static entry_iterator_t *entry_iterator(mrp_resmgr_usecase_t *usecase);



mrp_resmgr_usecase_t *mrp_resmgr_usecase_create(mrp_resmgr_t *resmgr)
{
    mrp_resmgr_usecase_t *usecase;
    mrp_htbl_config_t cfg;
    MRP_ASSERT(resmgr, "invalid argument");

    cfg.nentry = MRP_RESMGR_USECASE_SIZE_MAX;
    cfg.comp = mrp_string_comp;
    cfg.hash = mrp_string_hash;
    cfg.free = hash_free;
    cfg.nbucket = MRP_RESMGR_USECASE_SIZE_BUCKETS;

    if ((usecase = mrp_allocz(sizeof(mrp_resmgr_usecase_t)))) {
        usecase->resmgr = resmgr;
        usecase->size = 0;
        usecase->vector = NULL;
        usecase->entries = mrp_htbl_create(&cfg);
    }

    return usecase;
}

void mrp_resmgr_usecase_destroy(mrp_resmgr_usecase_t *usecase)
{
    if (usecase) {
        mrp_htbl_destroy(usecase->entries, true);
        mrp_free(usecase->updates);
        mrp_free(usecase->vector);
        mrp_free(usecase);
    }
}


ssize_t mrp_resmgr_usecase_add_attribute(mrp_resmgr_usecase_t *usecase,
                                         mrp_resmgr_source_t *source,
                                         const char *name)
{
    const char *srcnam;
    uint32_t connno;
    attr_def_t *atd;
    ssize_t offs;
    void *void_idx;
    int32_t attr_idx;
    uint32_t idx;
    uint32_t vect_idx;
    uint32_t upd_idx;
    update_t *upd;
    char buf[256];

    MRP_ASSERT(usecase && source && name, "invalid attribute");

    offs = -1;
    atd = NULL;

    if (parse_name(name, &srcnam, &connno, &atd, buf,sizeof(buf))) {

        if (!(void_idx = mrp_htbl_lookup(usecase->entries, (void *)name))) {
            if ((attr_idx = atd->idx) >= 0) {
                idx = mrp_resmgr_backend_get_attribute_index(atd->name,
                                                             mqi_integer);
                if (idx != MRP_RESMGR_RESOURCE_FIELD_INVALID)
                    attr_idx = idx;
                else {
                    mrp_log_error("gam-resource-manager: can't obtain index "
                                  "for resource attribute '%s'", atd->name);
                    goto out;
                }
            }

            vect_idx = add_entry_to_vector(usecase);
            upd_idx = add_update_to_usecase(usecase, attr_idx, vect_idx,
                                            atd->type);
            upd = usecase->updates + upd_idx;
            void_idx = NULL + (upd_idx + 1);

            if (!mrp_htbl_insert(usecase->entries,mrp_strdup(name),void_idx)) {
                mrp_log_error("gam-resource-manager: failed to insert into "
                              "hashtable usecase attribute '%s'", name);
            }

            if (atd->type == STAMP)
                add_stamp_to_usecase(usecase, vect_idx);
        }
        else {
            upd_idx = (void_idx - NULL) - 1;
            upd = usecase->updates + upd_idx;

            MRP_ASSERT(usecase->updates && (int)upd_idx < usecase->nupdate,
                       "corrupt data structures");

            vect_idx = upd->vector_idx;
        }

        if (!strcmp(srcnam, mrp_resmgr_source_get_name(source))) {
            upd->source = source;
            upd->connno = connno;
        }

        if (atd && atd->type == ROUTE)
            register_sink_ids(usecase, name, source);

        offs = sizeof(usecase->vector[0]) * vect_idx;
    }

 out:
    return offs;
}




void mrp_resmgr_usecase_update(mrp_resmgr_usecase_t *usecase)
{
    update_t *upd;
    mrp_resmgr_resource_t *ar;
    int32_t *val;
    int32_t idx;
    int i;

    MRP_ASSERT(usecase, "invalid argument");

    for (i =0;  i < usecase->nupdate;  i++) {
        upd = usecase->updates + i;
        val = usecase->vector + upd->vector_idx;

        if (!(ar = mrp_resmgr_source_get_resource(upd->source, upd->connno)))
            *val = 0;
        else {
            if ((idx = upd->attr_idx) >= 0)
                *val = mrp_resmgr_backend_get_integer_attribute(ar, idx);
            else {
                switch (idx) {
                case MRP_RESMGR_RESOURCE_FIELD_STATE:
                    *val = mrp_resmgr_backend_get_resource_state(ar);
                    break;
                case MRP_RESMGR_RESOURCE_FIELD_DECISION_ID:
                    *val = mrp_resmgr_backend_get_resource_decision_id(ar);
                    break;
                default:
                    *val = 0;
                    break;
                }
            }
        }
    }

    normalise_stamps(usecase->vector, usecase->nstamp, usecase->stamps);
}

void *mrp_resmgr_usecase_get_decision_input(mrp_resmgr_usecase_t *usecase)
{
    if (!usecase)
        return NULL;

    return (void *)usecase->vector;
}


size_t mrp_usecase_print(mrp_resmgr_usecase_t *usecase, char *buf, size_t len)
{
#define PRINT(args...) do { if (p<e) p += snprintf(p, e-p, args); } while(0)

    mrp_decision_conf_t *conf;
    entry_iterator_t *it;
    entry_descr_t *desc;
    const char *name;
    update_t *upd;
    int32_t value;
    const char *value_str;
    char *p, *e;
    char nbuf[256];
    bool er;
    int i;

    conf = mrp_resmgr_sources_get_decision_conf(usecase->resmgr);

    snprintf(buf, len, "<no entries>");

    e = (p = buf) + len;

    if (usecase && buf && len > 0 && (it = entry_iterator(usecase))) {
        for (i = 0;  i < it->nentry;  i++) {
            desc  = it->descs + i;
            name  = desc->name;
            upd   = usecase->updates + desc->update_idx;
            value = usecase->vector[upd->vector_idx];

            snprintf(nbuf, sizeof(nbuf), "%s:", name);

            value_str = mrp_decision_get_integer_attr_name(conf,name,value,&er);

            PRINT("%-24s %s (%d)\n", nbuf, value_str, value);
        }

        mrp_free(it);
    }

    return p - buf;

#undef PRINT
}


static void hash_free(void *key, void *object)
{
    MRP_UNUSED(object);
    mrp_free(key);
}

/*
 * the syntax of a regular attribute name is as follows:
 *
 *   - source name, eg. navigator
 *
 *   - optional digits specifying the connection number
 *     For the first connection, no digits are presents.
 *     Numbering of subsequent connections start from 2.
 *
 *   - suffix that actually defines the type of the attribute
 *     suffix names and the associated type definitions are
 *     in the filed_defs[] array.
 */
bool parse_name(const char *name, const char **source, uint32_t *connno,
                attr_def_t **attrdef, char *buf, int len)
{
    attr_def_t *d;
    char *suffix;
    char *p;
    uint32_t n;
    int l;

    MRP_ASSERT(name && source && connno && attrdef && buf && len > 0,
               "invalid arument");

    *source  = NULL;
    *connno  = 0;
    *attrdef = NULL;

    if (len < (l = strlen(name)) + 1)
        return false;

    memcpy(buf, name, l+1);
    buf[l] = 0;


    for (d = attr_defs;  d->suffix;  d++) {
        if ((suffix = strstr(buf, d->suffix)) && !suffix[d->len]) {
            *suffix = 0;

            for(n = 0, p = suffix - 1;  p >= buf && isdigit(*p);  p++)
                ;
            if (++p < suffix) {
                if ((n = strtoul(p, NULL, 10)) < 2)
                    return false;
                *p = 0;
            }

            if (p <= buf)
                return false;

            *source  = buf;
            *connno  = n;
            *attrdef = d;

            return true;
        }
    }

    return false;
}


static uint32_t add_update_to_usecase(mrp_resmgr_usecase_t *usecase,
                                      int32_t attr_idx,
                                      uint32_t vector_idx,
                                      attr_type_t type)
{
    uint32_t i = usecase->nupdate++;
    size_t size = sizeof(update_t) * usecase->nupdate;
    update_t *u;

    usecase->updates = mrp_realloc(usecase->updates, size);

    MRP_ASSERT(usecase->updates, "can't allocate memory");

    u = usecase->updates + i;
    u->source     = NULL;
    u->connno     = 0;
    u->attr_idx   = attr_idx;
    u->vector_idx = vector_idx;
    u->stamp      = (type == STAMP);

    return i;
}

static uint32_t add_entry_to_vector(mrp_resmgr_usecase_t *usecase)
{
    uint32_t idx = usecase->nentry++;
    size_t size = sizeof(int32_t) * usecase->nentry;

    usecase->vector = mrp_realloc(usecase->vector, size);

    MRP_ASSERT(usecase->vector, "can't allocate memory");

    usecase->vector[idx] = 0;

    return idx;
}

static void add_stamp_to_usecase(mrp_resmgr_usecase_t *usecase,
                                 uint32_t vector_idx)
{
    uint32_t idx = usecase->nstamp++;
    size_t size = sizeof(uint32_t) * usecase->nstamp;

    usecase->stamps = mrp_realloc(usecase->stamps, size);

    MRP_ASSERT(usecase->stamps, "can't allocate memory");

    usecase->stamps[idx] = vector_idx;
}


static void register_sink_ids(mrp_resmgr_usecase_t *usecase,
                              const char *attrnam,
                              mrp_resmgr_source_t *source)
{
    mrp_resmgr_t *resmgr;
    mrp_decision_conf_t *conf;
    ssize_t i, nvalue;
    mrp_decision_attr_value_desc_t values[MRP_RESMGR_SINK_MAX];
    mrp_decision_attr_value_desc_t *dsc;

    resmgr = usecase->resmgr;

    if (source) {
        conf = mrp_resmgr_sources_get_decision_conf(usecase->resmgr);
        nvalue = mrp_decision_attr_value_list(conf, attrnam, values,
                                              MRP_RESMGR_SINK_MAX);
        for (i = 0;  i < nvalue;  i++) {
            dsc = values + i;

            if (!mrp_resmgr_sink_add(resmgr, dsc->name, dsc->value, source)) {
                mrp_log_error("gam-resource-manager: failed to add sink '%s' "
                              "with decision id %d", dsc->name, dsc->value);
            }
        }
    }
}

static void normalise_stamps(int32_t *vector, int nstamp, uint32_t *stamps)
{
    int sort[MRP_RESMGR_USECASE_SIZE_MAX];
    int32_t val, tmp;
    int i,j,n;

    if (nstamp > 0 && nstamp < MRP_RESMGR_USECASE_SIZE_MAX) {
        for (n = i = 0;  i < nstamp;  i++) {
            j = stamps[i];

            MRP_ASSERT(j >= 0 && j < MRP_RESMGR_USECASE_SIZE_MAX,
                       "invalid stamp index");

            if (!(val = vector[j]))
                continue;

            if (val < 0)
                vector[j] = 0;
            else
                sort[n++] = j;
        }

        for (i = 0;  i < n-1;  i++) {
            for (j = i + 1;  j < n;  j++) {
                if (vector[sort[i]] > vector[sort[j]]) {
                    tmp = sort[i];
                    sort[i] = sort[j];
                    sort[j] = tmp;
                }
            }
        }

        for (i = 0;  i < n;  i++)
            vector[sort[i]] = i + 1;
    }
}

static int entry_iterator_cb(void *key, void *object, void *user_data)
{
    entry_iterator_t *it = (entry_iterator_t *)user_data;
    const char *name = (const char *)key;
    int update_idx = (object - NULL) - 1;
    entry_descr_t *desc;

    if (it->nentry < it->maxentry) {
        desc = it->descs + it->nentry++;
        desc->name = name;
        desc->update_idx = update_idx;
    }

    return MRP_HTBL_ITER_MORE;
}

static entry_iterator_t *entry_iterator(mrp_resmgr_usecase_t *usecase)
{
    entry_iterator_t *it;
    size_t size;
    entry_descr_t tmp;
    int i,j;


    if (!usecase)
        it = NULL;
    else {
        size = sizeof(*it) + (sizeof(entry_descr_t) * usecase->nentry);

        if ((it = mrp_allocz(size))) {
            it->maxentry = usecase->nentry;
            mrp_htbl_foreach(usecase->entries, entry_iterator_cb, it);
        }

        for (i = 0;  i < it->nentry - 1;  i++) {
            for (j = i + 1;  j < it->nentry;  j++) {
                if (it->descs[i].update_idx > it->descs[j].update_idx) {
                    tmp = it->descs[i];
                    it->descs[i] = it->descs[j];
                    it->descs[j] = tmp;
                }
            }
        }
    }

    return it;
}
