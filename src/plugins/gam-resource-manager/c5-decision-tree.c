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

#include "c5-decision-tree.h"


#define ATTRIBUTE_MAX  32
#define ENUM_MAX       16384
#define ENUM_BUCKETS   16



typedef enum   state_e              state_t;
typedef struct conf_iter_s          conf_iter_t;
typedef struct attr_value_iter_s    attr_value_iter_t;
typedef struct value_list_item_s    value_list_item_t;
typedef struct value_list_s         value_list_t;

enum state_e {
    START = 0,
    NAME,
    VALUE,
    END
};


struct conf_iter_s {
    mrp_decision_conf_t *conf;
    int nattr;
    mrp_decision_attr_t *attrs[0];
};


struct attr_value_iter_s {
    mrp_decision_attr_t *attr;
    int ndesc;
    mrp_decision_attr_value_desc_t descs[0];
};

struct value_list_item_s {
    const char *name;
    int32_t value;
};

struct value_list_s {
    int size;
    value_list_item_t *items;
};

static bool conf_finish(mrp_decision_conf_t *, const char *);
static conf_iter_t *conf_iterator(mrp_decision_conf_t *);

static mrp_decision_attr_t *attr_create(mrp_decision_conf_t*,const char*,int);
static void attr_destroy(void *, void *);
static void attr_add_value(mrp_decision_attr_t *, const char *);
static attr_value_iter_t *attr_value_iterator(mrp_decision_attr_t *);
static size_t attr_print(mrp_decision_attr_t *, bool, char *, size_t);

static void value_destroy(void *, void *);

static bool tree_parse(mrp_decision_conf_t *, FILE *, size_t *, char *,
                       mrp_decision_node_t **, int);
static bool tree_parse_terminal_node(mrp_decision_conf_t *, FILE *, size_t *,
                                     char *, bool, mrp_decision_node_t **, int);
static bool tree_parse_test_node(mrp_decision_conf_t *, FILE *, size_t *,
                                 char *, int, mrp_decision_node_t **, int);



static bool property(char **, char **, char **);
static bool list_item(char **, char **);
static bool identifier(char **, char **, char *);
static bool whitespace(char **);
static bool quoted(char **, char **);

static size_t print_node(mrp_decision_conf_t *, mrp_decision_node_t *,
                         conf_iter_t *, char *, size_t, char *);
static size_t print_bitmask(mrp_decision_attr_t *, mrp_decision_bitmask_t,
                            char *, size_t);



mrp_decision_conf_t *mrp_decision_conf_create_from_file(const char *stem)
{
    FILE *f;
    char filnam[1024];
    char *buf, *p;
    size_t n;
    ssize_t linlen;
    state_t state;
    char decision[256];
    char *name;
    char *value;
    char sep;
    size_t lineno;
    mrp_decision_conf_t *conf;
    mrp_decision_attr_t *attr;
    mrp_htbl_config_t aconf;
    int id;

    if (!stem || !stem[0])
        return false;

    snprintf(filnam, sizeof(filnam), "%s.names", stem);
    if (!(f = fopen(filnam, "r"))) {
        mrp_log_error("gam-resource-manager: failed to open file '%s': %s",
                      filnam, strerror(errno));
        return NULL;
    }

    if (!(conf = mrp_allocz(sizeof(mrp_decision_conf_t)))) {
        mrp_log_error("gam-resource-manager: can't allocate memory for "
                      "'%s' decision configuration", stem);
        return NULL;
    }
    else {
        aconf.nentry  = ATTRIBUTE_MAX;
        aconf.comp    = mrp_string_comp;
        aconf.hash    = mrp_string_hash;
        aconf.free    = attr_destroy;
        aconf.nbucket = ATTRIBUTE_MAX;

        conf->stem  = mrp_strdup(stem);
        conf->nattr = 0;
        conf->attrs = mrp_htbl_create(&aconf);

        if (!conf->attrs) {
            mrp_log_error("gam-resource-manager: failed to create attribute "
                          "hash for '%s' decision configuration", stem);
            mrp_decision_conf_destroy(conf);
            return NULL;
        }
    }

    state = START;
    buf = NULL;
    lineno = 0;
    attr = NULL;
    id = 0;
    decision[0] = 0;

    while ((linlen = getline(&buf, &n, f)) >= 0) {
        lineno++;
        p = buf;

        whitespace(&p);

        while (*p) {
            switch (state) {

            case START:
                if (!identifier(&p, &name, &sep) || sep != '.')
                    goto failed;
                mrp_debug("decision = '%s'", name);
                snprintf(decision, sizeof(decision), "%s", name);
                state = NAME;
                whitespace(&p);
                break;

            case NAME:
                if (!identifier(&p, &name, &sep) || sep != ':')
                    goto failed;
                mrp_debug("name = '%s'", name);
                if (!(attr = attr_create(conf, name, id++)))
                    goto failed;
                state = VALUE;
                whitespace(&p);
                break;

            case VALUE:
                if (!identifier(&p, &value, &sep) ||
                    (sep != ',' && sep != '.' && sep != 0))
                    goto failed;
                mrp_debug("value = '%s'", value);
                if (!strcmp(name, "continuous") && sep != '.')
                    goto failed;
                attr_add_value(attr, value);
                if (sep == '.') {
                    state = NAME;
                    attr = NULL;
                }
                whitespace(&p);
                break;

            default:
                break;
            }
        }

        free(buf);
        buf = NULL;
    } /* while getline */

    if (linlen < 0 && !feof(f)) {
        mrp_log_error("gam-resource-manager: error during reading "
                      "'%s' file: %s", filnam, strerror(errno));
        mrp_decision_conf_destroy(conf);
        return NULL;
    }

    fclose(f);

    if (!conf_finish(conf, decision)) {
        mrp_decision_conf_destroy(conf);
        return NULL;
    }

    mrp_log_info("mrp-resource-manager: successfully loaded "
                 "decision configuration from file '%s'", filnam);

    return conf;

  failed:
    mrp_log_error("gam-resource-manager: error in file '%s' line %zu",
                  filnam, lineno);
    free(buf);
    fclose(f);
    mrp_decision_conf_destroy(conf);
    return NULL;
}

void mrp_decision_conf_destroy(mrp_decision_conf_t *conf)
{
    if (conf) {
        mrp_log_info("mrp-resource-manager: going to unload "
                     "decision configuration '%s'", conf->stem);

        if (conf->decision_attr && conf->decision_names)
            mrp_free(conf->decision_names);

        mrp_htbl_destroy(conf->attrs, TRUE);

        mrp_free((void *)conf->stem);
        mrp_free((void *)conf);
    }
}

bool mrp_decision_set_attr_offset(mrp_decision_conf_t *conf,
                                  const char *attr_name,
                                  size_t offset)
{
    mrp_decision_attr_t *attr;

    if (!conf || !attr_name)
        return false;

    if (!(attr = mrp_htbl_lookup(conf->attrs, (void *)attr_name)))
        return false;

    attr->offset = offset;

    return true;
}

ssize_t mrp_decision_attr_list(mrp_decision_conf_t *conf,
                               const char **buf, size_t len)
{
    conf_iter_t *it;
    int i,j,n;

    if (!conf || !buf || len < 1)
        return -1;

    if ((int)len < conf->nattr)
        return -1;

    if (!(it = conf_iterator(conf)))
        return -1;

    for (i = j = 0, n = it->nattr;  i < n;  i++) {
        if (conf->decision_attr != it->attrs[i])
            buf[j++] = it->attrs[i]->name;
    }
    buf[j] = NULL;

    mrp_free(it);

    return j;
}

ssize_t mrp_decision_attr_value_list(mrp_decision_conf_t *conf,
                                     const char *attr_name,
                                     mrp_decision_attr_value_desc_t *buf,
                                     size_t len)
{
    mrp_decision_attr_t *attr;
    attr_value_iter_t *it;
    int buf_len;
    int actual_len;
    size_t size;

    if (!conf || !attr_name || !buf || (buf_len = len) < 1)
        return -1;

    if (!(attr = mrp_htbl_lookup(conf->attrs, (void *)attr_name)))
        return -1;

    if (buf_len < attr->nvalue)
        return -1;

    if (!(it = attr_value_iterator(attr)))
        return -1;

    actual_len = it->ndesc;

    size = sizeof(mrp_decision_attr_value_desc_t) * actual_len;
    memcpy(buf, it->descs, size);

    if (buf_len > actual_len) {
        size = sizeof(mrp_decision_attr_value_desc_t) * (buf_len - it->ndesc);
        memset(buf + it->ndesc, 0, size);
    }

    mrp_free(it);

    return actual_len;
}



const char *mrp_decision_name(mrp_decision_conf_t *conf, int32_t decision)
{
    if (!conf || decision < 0 || decision > conf->decision_attr->nvalue)
        return "<invalid>";

    return conf->decision_names[decision];
}


int32_t mrp_decision_value_max(mrp_decision_conf_t *conf)
{
    if (!conf || !conf->decision_attr)
        return 0;

    return conf->decision_attr->nvalue;
}

int32_t mrp_decision_get_integer_attr_value(mrp_decision_conf_t *conf,
                                            const char *attr_name,
                                            const char *value_name,
                                            bool *error)
{
    mrp_decision_attr_t *attr;
    mrp_decision_value_t *value;

    if (error)
        *error = true;

    if (!conf || !attr_name || !value_name)
        return 0;

    if (!(attr = mrp_htbl_lookup(conf->attrs, (void *)attr_name)))
        return -1;

    if (attr->value_type != MRP_DECISION_VALUE_INTEGER)
        return -1;

    if (!(value = mrp_htbl_lookup(attr->values, (void *)value_name)))
        return -1;

    if (error)
        *error = false;

    return value->integer;
}


const char *mrp_decision_get_integer_attr_name(mrp_decision_conf_t *conf,
                                               const char *attr_name,
                                               int32_t value,
                                               bool *error)
{
    mrp_decision_attr_t *attr;
    attr_value_iter_t *it;
    const char *value_name;
    int i;

    if (error)
        *error = true;

    if (!conf || !attr_name)
        return "<error>";

    if (!(attr = mrp_htbl_lookup(conf->attrs, (void *)attr_name)))
        return "<unknown attribute>";

    if (attr->value_type != MRP_DECISION_VALUE_INTEGER)
        return "<not integer attribute>";

    if (!(it = attr_value_iterator(attr)))
        return "<error>";

    for (i = 0, value_name = "<unknown value>";   i < it->ndesc;   i++) {
        if (it->descs[i].value == value) {
            value_name = it->descs[i].name;
            break;
        }
    }

    if (i < it->ndesc && error)
        *error = false;

    return value_name;
}


size_t mrp_decision_conf_print(mrp_decision_conf_t *conf, char *buf,size_t len)
{
    conf_iter_t *it;
    mrp_decision_attr_t *attr;
    int i;
    char *p, *e;

    e = (p = buf) + len;

    if (buf) {
        if (!(it = conf_iterator(conf)))
            p += snprintf(p, e-p, "<error>");
        else {
            p += snprintf(p, e-p, "attributes for '%s'\n", conf->stem);

            for (i = 0;  i < it->nattr;  i++) {
                attr = it->attrs[i];
                p += attr_print(attr, (attr == conf->decision_attr), p, e-p);
            }

            mrp_free(it);
        }
    }

    return p - buf;
}


static bool conf_finish(mrp_decision_conf_t *conf,
                        const char *decision_attr_name)
{
    mrp_decision_attr_t *attr;
    attr_value_iter_t *it;
    mrp_decision_attr_value_desc_t *dsc;
    size_t size;
    const char **tbl;
    int i, n;

    if (!(attr = mrp_htbl_lookup(conf->attrs, (void *)decision_attr_name))) {
        mrp_log_error("gam-resoure-manager: can't find decision attribute "
                      "'%s' for '%s'", decision_attr_name, conf->stem);
        return false;
    }

    if (attr->nvalue < 1) {
        mrp_log_error("gam-resource-manager: attribute '%s' in '%s' is "
                      "not suitable for decisions", attr->name, conf->stem);
        return false;
    }

    n = attr->nvalue;
    size = sizeof(const char *) * n;

    if (!(it = attr_value_iterator(attr)) || !(tbl = mrp_allocz(size))) {
        mrp_free(it);
        mrp_log_error("gam-resource-manager: can't allocate memory to "
                      "finalize '%s' decision", conf->stem);
        return false;
    }

    for (i = 0;  i < n;  i++) {
        dsc = it->descs + i;

        if (i != dsc->value) {
            mrp_log_error("gam-resource-manager: internal error: decision "
                          "values of '%s' are non-continous for '%s' decisions",
                          attr->name, conf->stem);
            mrp_free(it);
            mrp_free(tbl);
            return false;
        }

        tbl[i] = dsc->name;
    };

    mrp_free(it);

    conf->decision_attr = attr;
    conf->decision_names = tbl;

    return true;
}


static int conf_iter_cb(void *key, void *object, void *user_data)
{
    conf_iter_t *it = (conf_iter_t *)user_data;
    mrp_decision_attr_t *attr = (mrp_decision_attr_t *)object;

    MRP_UNUSED(key);

    if (it->nattr >= it->conf->nattr) {
        mrp_log_error("gam-resource-manager: detected inconsitency while "
                      "iterating configuration of '%s'", it->conf->stem);
    }
    else {
        it->attrs[it->nattr++] = attr;
    }

    return MRP_HTBL_ITER_MORE;
}

static conf_iter_t *conf_iterator(mrp_decision_conf_t *conf)
{
    conf_iter_t *it;
    size_t size;
    mrp_decision_attr_t *tmp;
    int i,j;

    if (!conf)
        it = NULL;
    else {
        size = sizeof(conf_iter_t) + (sizeof(void *) * conf->nattr);

        if ((it = mrp_allocz(size))) {
            it->conf = conf;

            mrp_htbl_foreach(conf->attrs, conf_iter_cb, it);

            for (i = 0;   i < it->nattr - 1;   i++) {
                for (j = i + 1;   j < it->nattr;   j++) {
                    if (it->attrs[i]->id > it->attrs[j]->id) {
                        tmp = it->attrs[i];
                        it->attrs[i] = it->attrs[j];
                        it->attrs[j] = tmp;
                    }
                }
            }
        }
    }

    return it;
}


static mrp_decision_attr_t *attr_create(mrp_decision_conf_t *conf,
                                        const char *name, int id)
{
    mrp_decision_attr_t *attr;

    if (!conf || !name)
        attr = NULL;
    else {
        if ((attr = mrp_allocz(sizeof(mrp_decision_attr_t)))) {
            attr->name       = mrp_strdup(name);
            attr->id         = id;
            attr->attr_type  = MRP_DECISION_ATTR_CONTINUOUS;
            attr->value_type = MRP_DECISION_VALUE_INTEGER;
            attr->nvalue     = 0;
            attr->values     = NULL;
        }

        if (!mrp_htbl_insert(conf->attrs, (void *)attr->name, attr)) {
            attr_destroy((void *)attr->name, (void *)attr);
            attr = NULL;
        }

        conf->nattr++;
    }

    return attr;
}

static void attr_destroy(void *key, void *object)
{
    mrp_decision_attr_t *attr = (mrp_decision_attr_t *)object;

    MRP_UNUSED(key);

    if (attr->values)
        mrp_htbl_destroy(attr->values, TRUE);

    mrp_free((void *)attr->name);
    mrp_free(object);
}

static void attr_add_value(mrp_decision_attr_t *attr, const char *name)
{
    mrp_htbl_config_t vconf;
    mrp_decision_value_t *value;
    char *key = NULL;

    if (attr && name) {
        if (attr->attr_type == MRP_DECISION_ATTR_CONTINUOUS) {
            vconf.nentry  = ENUM_MAX;
            vconf.comp    = mrp_string_comp;
            vconf.hash    = mrp_string_hash;
            vconf.free    = value_destroy;
            vconf.nbucket = ENUM_BUCKETS;

            attr->attr_type = MRP_DECISION_ATTR_ENUM;
            attr->nvalue    = 0;
            attr->values    = mrp_htbl_create(&vconf);
        }

        if (!(key = mrp_strdup(name)) || !(value = mrp_allocz(sizeof(*value))))
            mrp_free((void *)key);
        else {
            value->integer = attr->nvalue++;
            mrp_htbl_insert(attr->values, key, value);
        }
    }
}

static int attr_value_iter_cb(void *key, void *object, void *user_data)
{
    attr_value_iter_t *it = (attr_value_iter_t *)user_data;
    const char *name = (const char *)key;
    mrp_decision_value_t *value = (mrp_decision_value_t *)object;
    mrp_decision_attr_value_desc_t *desc;

    if (it->ndesc >= it->attr->nvalue) {
        mrp_log_error("gam-resource-manager: detected inconsitency while "
                      "iterating decision attribute '%s' for '%s'",
                      name, it->attr->name);
    }
    else {
        desc = it->descs + it->ndesc++;
        desc->name = name;
        desc->value = value->integer;
    }

    return MRP_HTBL_ITER_MORE;
}

static attr_value_iter_t *attr_value_iterator(mrp_decision_attr_t *attr)
{
    attr_value_iter_t *it;
    mrp_decision_attr_value_desc_t tmp;
    size_t size;
    int i,j;

    if (!attr)
        it = NULL;
    else {
        size = sizeof(*it) +
               (sizeof(mrp_decision_attr_value_desc_t) * attr->nvalue);

        if ((it = mrp_allocz(size))) {
            it->attr = attr;
            it->ndesc = 0;
            mrp_htbl_foreach(attr->values, attr_value_iter_cb, it);

            for (i = 0;   i < it->ndesc - 1;   i++) {
                for (j = i + 1;  j < it->ndesc;   j++) {
                    if (it->descs[i].value > it->descs[j].value) {
                        tmp = it->descs[i];
                        it->descs[i] = it->descs[j];
                        it->descs[j] = tmp;
                    }
                }
            }
        }
    }

    return it;
}

static size_t attr_print(mrp_decision_attr_t *attr, bool decision,
                         char *buf, size_t len)
{
#define PRINT(args...) \
    do { if (p < e) p += snprintf(p, e-p, args); } while (0)

    attr_value_iter_t *it;
    mrp_decision_attr_value_desc_t *dsc;
    char *p, *e;
    const char *sep;
    char nambuf[256];
    int i;

    e = (p = buf) + len;

    if (attr && p < e) {
        snprintf(nambuf, sizeof(nambuf), "%2d %s:", attr->id, attr->name);
        PRINT(" %c %-24s @%03zu  ", decision ? '*':' ', nambuf, attr->offset);

        switch (attr->attr_type) {

        case MRP_DECISION_ATTR_ENUM:
            if (!(it = attr_value_iterator(attr)))
                PRINT("<error>\n");
            else {
                PRINT("{");
                for (i = 0;   i < it->ndesc;   i++) {
                    dsc = it->descs + i;
                    sep = (i == 0) ?  ""   : ((i % 10) ?
                                      ", " :
                                      ",\n                                  ");
                    PRINT("%s[%d (%s)]", sep, dsc->value, dsc->name);
                }
                PRINT("}\n");

                mrp_free(it);
            }
            break;

        case MRP_DECISION_ATTR_CONTINUOUS:
            PRINT("continuous\n");
            break;

        default:
            PRINT("<unsupported attribute type %d>\n", attr->attr_type);
            break;
        }
    }

    return p - buf;

#undef PRINT
}

static void value_destroy(void *key, void *object)
{
    mrp_free(key);
    mrp_free(object);
}


mrp_decision_node_t *mrp_decision_tree_create_from_file(
                                             mrp_decision_conf_t *conf,
                                             const char *stem)
{
    FILE *f;
    char filnam[1024];
    char *buf, *p;
    size_t n;
    ssize_t linlen;
    size_t lineno;
    mrp_decision_node_t *root, *node;
    mrp_decision_value_type_t vtype;
    char *name, *value;

    if (!stem)
        stem = conf->stem;

    snprintf(filnam, sizeof(filnam), "%s.tree", stem);
    if (!(f = fopen(filnam, "r"))) {
        printf("failed to open file '%s': %s\n", filnam, strerror(errno));
        return NULL;
    }

    vtype = conf->decision_attr->value_type;

    if (!(root = mrp_decision_tree_create(stem, vtype))) {
        mrp_log_error("gam-resource-manager: failed to create "
                      "decision tree for '%s'", stem);
        return NULL;
    }

    lineno = 0;
    buf = NULL;
    node = NULL;

    while ((linlen = getline(&buf, &n, f)) >= 0) {
        lineno++;
        p = buf;

        whitespace(&p);

        if (!strncmp(p, "type", 4)) {
            if (!(tree_parse(conf, f, &lineno, buf, &node, 0)))
                goto failed;
            if (!(mrp_decision_add_node_to_root(root, node)))
                goto failed;
            node = NULL;
        }
        else if (property(&p, &name, &value)) {
            if (!strcmp(name, "id")) {
                mrp_debug("id: %s", value);
            }
            else if (!strcmp(name, "entries")) {
                mrp_debug("entries: %s", value);
            }
            else {
                goto parse_error;
            }
        }

        free(buf);
        buf = NULL;
    }

    if (linlen < 0 && !feof(f)) {
        mrp_log_error("gam-resource-manager: error during reading "
                      "'%s' file: %s", filnam, strerror(errno));
        goto failed;
    }

    fclose(f);

    mrp_log_info("mrp-resource-manager: successfully loaded "
                 "decision tree from file '%s'", filnam);

    return root;

 parse_error:
    mrp_log_error("gam-resource-manager: error in file '%s' line %zu",
                  filnam, lineno);
 failed:
    mrp_log_error("gam-resource-manager: failed to parse '%s' file",
                  filnam);
    free(buf);
    fclose(f);
    mrp_decision_tree_destroy(root);
    mrp_decision_tree_destroy(node);
    return NULL;
}

static const char *indent(int depth)
{
    static char buf[1024];
    size_t l = depth * 3;
    memset(buf, ' ', l);
    buf[l] = 0;
    return buf;
}

static bool tree_parse(mrp_decision_conf_t *conf,
                       FILE *f, size_t *lineno,
                       char *buf,
                       mrp_decision_node_t **node,
                       int depth)
{
    char *p = buf;
    char *name, *value, *e;
    int type;

    if (!property(&p, &name, &value) || strcmp(name, "type"))
        return false;

    type = strtol(value, &e, 10);

    if (e == value || *e)
        return false;

    switch (type) {
    case 0:
        return tree_parse_terminal_node(conf, f, lineno, p, false, node, depth);

    case 1:
    case 2:
    case 3:
        return tree_parse_test_node(conf, f, lineno, p, type, node, depth);

    default:
        return false;
    }

    return true;
}

static bool tree_parse_terminal_node(mrp_decision_conf_t *conf,
                                     FILE *f, size_t *lineno,
                                     char *buf,
                                     bool need_empty,
                                     mrp_decision_node_t **node,
                                     int depth)
{
    char *p = buf;
    char *name, *value;
    mrp_decision_value_t *vptr;
    char *decision_name;
    int32_t decision;
    bool has_decision;
    bool has_cases;

    MRP_UNUSED(f);
    MRP_UNUSED(lineno);
    MRP_UNUSED(depth);

    has_decision = false;
    has_cases = false;

    while (*p) {
        if (!property(&p, &name, &value))
            break;

        if (!strcmp(name, "class")) {
            if (!(vptr = mrp_htbl_lookup(conf->decision_attr->values, value)))
                return false;
            else {
                decision_name = value;
                decision = vptr->integer;
                has_decision = true;
            }
        }
        if (!strcmp(name, "freq")) {
            has_cases = true;
        }

        whitespace(&p);
    }

    if (has_decision) {
        if (!need_empty && node) {
            mrp_debug("%sterminal: %d/%s", indent(depth),
                  decision, decision_name);
            if (!(*node = mrp_decision_create_terminal_node(vptr)))
                return false;
            return true;
        }
        if (need_empty && !has_cases)
            return true;
    }

    return false;
}

static bool tree_parse_test_node(mrp_decision_conf_t *conf,
                                 FILE *f, size_t *lineno,
                                 char *buf,
                                 int type,
                                 mrp_decision_node_t **node,
                                 int depth)
{
    mrp_decision_node_t *child;
    char *p;
    char *name, *value;
    mrp_decision_attr_t *attr;
    int nbr;
    char *e;
    size_t n;
    char *buf2;
    int listidx;
    value_list_t *lists, *l;
    value_list_item_t *iv;
    mrp_decision_value_t *av;
    mrp_decision_value_type_t testval_type;
    mrp_decision_value_t testval;
    mrp_decision_condition_t testcond;
    int i,j,k;
    attr_value_iter_t *ait;
    char valbuf[4096], *q;
    size_t size;
    bool ok, success;
    char dbgbuf[256];

    p = buf;
    child = NULL;
    attr = NULL;
    nbr  = -1;
    listidx = 0;
    lists = NULL;
    buf2 = NULL;
    ait = NULL;
    success = false;

    testcond = MRP_DECISION_EQ;
    testval_type = MRP_DECISION_VALUE_UNKNOWN;
    memset(&testval, 0, sizeof(testval));

    while (*p) {
        if (!property(&p, &name, &value))
            break;

        if (!strcmp(name, "att")) {
            if (attr)
                goto finish_parsing;

            if (!(attr = mrp_htbl_lookup(conf->attrs, value)))
                goto finish_parsing;
        }
        else if (!strcmp(name, "forks")) {
            if (nbr >= 0)
                goto finish_parsing;

            nbr = strtol(value, &e, 10);

            if (*e || e == value || nbr <= 0 || nbr > 100)
                goto finish_parsing;

            if (type == 3)
                lists = mrp_allocz(sizeof(*lists) * nbr);
        }
        else if (!strcmp(name, "elts")) {
            if (!attr || !lists || listidx >= nbr)
                goto finish_parsing;

            l = lists + listidx++;
            l->items = mrp_allocz(sizeof(l->items[0]) * attr->nvalue);

            do {
                if (l->size >= attr->nvalue)
                    goto finish_parsing;
                if (!(av = mrp_htbl_lookup(attr->values, value)))
                    goto finish_parsing;

                iv = l->items + l->size++;
                iv->name = value;
                iv->value = av->integer;

            } while (list_item(&p, &value));
        }

        whitespace(&p);

    } /* while property */

    if (attr && nbr > 0) {
        if (type == 1) {
            if (getline(&buf2, &n, f) < 0)
                goto finish_parsing;

            if (!tree_parse_terminal_node(conf, f,lineno,buf2, true, NULL, 0))
                goto finish_parsing;

            free(buf2);
            buf2 = NULL;
            nbr--;
            if (!(ait = attr_value_iterator(attr)))
                goto finish_parsing;
        }

        mrp_debug("%stest/%d: '%s'", indent(depth), nbr, attr->name);

        if (!(*node = mrp_decision_create_test_node()))
            goto finish_parsing;

        for (i=0, buf2=NULL;  i < nbr && getline(&buf2,&n,f) >= 0;   i++) {
            (*lineno)++;

            switch (type) {

            case 1:
                testval_type = MRP_DECISION_VALUE_INTEGER;
                testval.integer = ait->descs[i].value;
                testcond = MRP_DECISION_EQ;
                snprintf(valbuf, sizeof(valbuf), "%s", ait->descs[i].name);
                break;

            case 2:
                break;

            case 3:
                testval_type = MRP_DECISION_VALUE_UNKNOWN;
                l = lists + i;
                if (l->size == 1) {
                    testcond = MRP_DECISION_EQ;
                    testval_type = MRP_DECISION_VALUE_INTEGER;
                    testval.integer = l->items[0].value;
                    snprintf(valbuf, sizeof(valbuf), "%s", l->items[0].name);
                }
                else {
                    testcond = MRP_DECISION_IN;
                    if (l->size <= (int)MRP_DECISION_BITMASK_WIDTH) {
                        testval_type = MRP_DECISION_VALUE_BITMASK;
                        testval.bitmask = 0;
                    }
                    else {
                        testval_type = MRP_DECISION_ARRAY |
                            MRP_DECISION_VALUE_INTEGER;
                        testval.array.size = l->size;
                        size = sizeof(mrp_decision_value_t) * testval.array.size;
                        testval.array.values = mrp_allocz(size);
                    }
                    e = (q = valbuf) + sizeof(valbuf);
                    for (j = 0;   j < l->size;   j++) {
                        if (q < e) {
                            q += snprintf(q, e-q, "%s%s",
                                          j?",":"", l->items[j].name);
                        }
                        k = l->items[j].value;
                        if (testval_type == MRP_DECISION_VALUE_BITMASK)
                            testval.bitmask |= MRP_DECISION_BIT(k);
                        else
                            testval.array.values[j].integer = k;
                    }
                }
                break;

            default:
                goto finish_parsing;
            }

            switch (testval_type) {
            case MRP_DECISION_VALUE_BITMASK:
                snprintf(dbgbuf, sizeof(dbgbuf), " 0x%x", testval.bitmask);
                break;
            case MRP_DECISION_VALUE_INTEGER:
                snprintf(dbgbuf, sizeof(dbgbuf), " %d", testval.integer);
                break;
            default:
                dbgbuf[0] = 0;
            }
            mrp_debug("%s%s %s '%s'%s", indent(depth+1),
                      mrp_decision_condition_str(testcond),
                      mrp_decision_value_type_str(testval_type),
                      valbuf, dbgbuf);

            if (!tree_parse(conf, f, lineno, buf2, &child, depth+2))
                goto finish_parsing;

            ok = mrp_decision_add_branch_to_test_node(*node, testcond, attr->id,
                                                      testval_type, &testval,
                                                      attr->offset, child);
            if (!ok)
                goto finish_parsing;

            child = NULL;
            if ((testval_type & MRP_DECISION_ARRAY))
                mrp_free(testval.array.values);
            testval_type = MRP_DECISION_VALUE_UNKNOWN;

            free(buf2);
            buf2 = NULL;
        }

        success = true;
    }

 finish_parsing:
    if (lists) {
        for (i = 0; i < nbr; i++)
            free(lists[i].items);
        mrp_free(lists);
    }
    if ((testval_type & MRP_DECISION_ARRAY))
        mrp_free(testval.array.values);
    mrp_decision_tree_destroy(child);
    mrp_free(ait);
    free(buf2);

    return success;
}



static bool property(char **buf, char **name, char **value)
{
    char *p = *buf;
    char term;

    if (identifier(&p, name, &term) &&
        term == '=' &&
        quoted(&p, value))
    {
        whitespace(&p);
        *buf = p;
        return true;
    }

    return false;
}

static bool list_item(char **buf, char **name)
{
    char *p = *buf;

    whitespace(&p);

    if (*p++ != ',')
        return false;

    if (quoted(&p, name)) {
        *buf = p;
        return true;
    }

    return false;
}


static bool identifier(char **buf, char **id, char *term)
{
    char *p, *q, c;

    q = *buf;

    whitespace(&q);

    for (p = q;  (c = *p);  p++) {
        if (!isalnum(c))
            break;
    }

    if (p == q)
        return false;

    whitespace(&p);
    if ((*term = *p))
        *p++ = 0;

    *buf = p;
    *id = q;
    return true;
}


static bool whitespace(char **buf)
{
    char *p, c;

    for (p = *buf;  (c = *p);  p++) {
        if (c != ' ' && c != '\t' && c != '\n')
            break;
    }

    *buf = p;
    return true;
}

static bool quoted(char **buf, char **string)
{
    char *p, *q, c;

    q = *buf;

    whitespace(&q);

    if (*q++ != '"')
        return false;

    for (p = q; (c = *p);  p++) {
        if (c < 0x20)
            return -1;
        if (c == '"' && p[-1] != '\\') {
            *p++ = 0;
            *buf = p;
            *string = q;
            return true;
        }
    }

    return false;
}

size_t mrp_decision_tree_print(mrp_decision_conf_t *conf,
                               mrp_decision_node_t *node,
                               char *buf, size_t len)
{
    conf_iter_t *cit;
    char *p, *e;

    e = (p = buf) + len;

    if (conf && node && buf && len > 0) {
        if (!(cit = conf_iterator(conf)))
            p += snprintf(p, e-p, "<error>\n");
        else {
            p += print_node(conf, node, cit, p, e-p, NULL);
            p += snprintf(p, e-p, "\n");
            mrp_free(cit);
        }
    }

    return p - buf;
}


static size_t print_node(mrp_decision_conf_t *conf,
                         mrp_decision_node_t *node,
                         conf_iter_t *cit,
                         char *buf, size_t len,
                         char *indent)
{
#define PRINT(_args...)      \
    do {if (p < e) p += snprintf(p,e-p, _args);} while(0)
#define PRINT_VALUE(_t,_v)   \
    do {if (p < e) p += mrp_decision_value_print(_t, _v, p,e-p);} while(0)
#define PRINT_BITMASK(_a,_m) \
    do {if (p < e) p += print_bitmask(_a, _m, p, e-p);} while(0)
#define PRINT_NODE(_p,_i)    \
    do {if (p < e) p += print_node(conf, (_p)->node, cit, p,e-p, _i);} while(0)

    static char indent_buf[4096];
    static char *indent_end = indent_buf + sizeof(indent_buf);

    mrp_decision_root_node_t *root;
    mrp_decision_test_node_t *test;
    mrp_decision_terminal_node_t *term;
    mrp_decision_branch_t *branch;
    mrp_decision_attr_t *attr;
    const char *attr_name;
    int32_t value_idx;
    attr_value_iter_t *ait;
    char *p, *e, *new_indent;
    int32_t decision;
    size_t i;

    if (!node || !buf)
        return 0;

    if (!indent) {
        indent_buf[0] = 0;
        indent = indent_buf;
    }

    e = (p = buf) + len;

    switch (node->type) {

    case MRP_DECISION_ROOT_NODE:
        root = &node->root;
        PRINT("root of %s (decision type: %s)", root->name,
              mrp_decision_value_type_str(root->decision_value_type));
        new_indent  = indent;
        new_indent += snprintf(indent, indent_end-indent, "\n");
        PRINT_NODE(root, new_indent);
        *indent = 0;
        break;

    case MRP_DECISION_TEST_NODE:
        test = &node->test;

        for (i = 0;  i < test->nbranch;  i++) {
            branch = test->branches + i;
            if (branch->value_id < 0 || branch->value_id >= cit->nattr)
                PRINT("%s:...<invalid attribute>", indent_buf);
            else {
                attr = cit->attrs[branch->value_id];
                attr_name = attr ? attr->name : "<invalid attribute>";
                PRINT("%s:...%s %s ", indent_buf, attr_name,
                      mrp_decision_condition_str(branch->condition));

                if (branch->value_type != MRP_DECISION_VALUE_INTEGER) {
                    PRINT_VALUE(branch->value_type, &branch->value);
                    if (branch->value_type == MRP_DECISION_VALUE_BITMASK) {
                        PRINT(" (");
                        PRINT_BITMASK(attr, branch->value.bitmask);
                        PRINT(")");
                    }
                }
                else {
                    value_idx = branch->value.integer;
                    if (value_idx < 0 || value_idx >= attr->nvalue ||
                        !(ait = attr_value_iterator(attr)))
                        PRINT("<invalid attribute value>");
                    else {
                        PRINT("%d (%s)", value_idx, ait->descs[value_idx].name);
                        mrp_free(ait);
                    }
                }
            }
            new_indent  = indent;
            new_indent += snprintf(new_indent, indent_end-indent, "%c   ",
                                   i == test->nbranch-1 ? ' ':':');
            PRINT_NODE(branch, new_indent);
            *indent = 0;
        }

        break;

    case MRP_DECISION_TERMINAL_NODE:
        term = &node->terminal;
        decision = term->decision.integer;
        if (decision < 0 || decision >= conf->decision_attr->nvalue)
            PRINT(" => decision <invalid value>");
        else
            PRINT(" => %s", conf->decision_names[decision]);
        break;

    default:
        PRINT("%s<unknown node type %d>\n", indent_buf, node->type);
        break;
    }

    return p - buf;

#undef PRINT_VALUE
#undef PRINT
}


static size_t print_bitmask(mrp_decision_attr_t *attr,
                            mrp_decision_bitmask_t bitmask,
                            char *buf, size_t len)
{
    attr_value_iter_t *it;
    mrp_decision_bitmask_t m;
    char *p, *e;
    int i,j;
    char *sep;

    if (!attr || !buf || len < 1)
        return 0;

    if (!(it = attr_value_iterator(attr)))
        return 0;

    e = (p = buf) + len;

    if (!(m = bitmask))
        p += snprintf(p, e-p, "<empty>");
    else {
        for (i = 0, sep = "";   m && i < it->ndesc && p < e;  i++, m >>= 1) {
            if ((m & 1)) {
                if (it->descs[i].value == i)
                    j = i;
                else {
                    for (j = 0;  j < it->ndesc;  j++) {
                        if (it->descs[j].value == i)
                            break;
                    }
                }
                if (j < 0 || j >= it->ndesc)
                    p += snprintf(p, e-p, "%s<unknown value %d>", sep, j);
                else
                    p += snprintf(p, e-p, "%s%s", sep, it->descs[j].name);
                sep = ",";
            }
        }
    }

    mrp_free(it);

    return p - buf;
}
