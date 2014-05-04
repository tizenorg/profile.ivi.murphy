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
#ifndef __MRP_C5_DECISION_TREE_H__
#define __MRP_C5_DECISION_TREE_H__

#include <stdint.h>
#include <stdbool.h>

#include <murphy/common/hashtbl.h>

#include "decision-tree.h"

typedef enum  mrp_decision_attr_type_e         mrp_decision_attr_type_t;
typedef struct mrp_decision_attr_s             mrp_decision_attr_t;
typedef struct mrp_decision_conf_s             mrp_decision_conf_t;
typedef struct mrp_decision_attr_value_desc_s  mrp_decision_attr_value_desc_t;

enum mrp_decision_attr_type_e {
    MRP_DECISION_ATTR_UNKNOWN = 0,
    MRP_DECISION_ATTR_ENUM,
    MRP_DECISION_ATTR_CONTINUOUS,
};

struct mrp_decision_attr_s {
    const char *name;
    int id;
    mrp_decision_attr_type_t attr_type;
    mrp_decision_value_type_t value_type;
    size_t offset;
    int nvalue;
    mrp_htbl_t *values;
};


struct mrp_decision_conf_s {
    const char *stem;
    int nattr;
    mrp_htbl_t *attrs;
    mrp_decision_attr_t *decision_attr;
    const char **decision_names;
};


struct mrp_decision_attr_value_desc_s {
    const char *name;
    int32_t value;
};


mrp_decision_conf_t *mrp_decision_conf_create_from_file(const char *stem);
void mrp_decision_conf_destroy(mrp_decision_conf_t *conf);

bool mrp_decision_set_attr_offset(mrp_decision_conf_t *conf,
                                  const char *attr_name, size_t offset);

ssize_t mrp_decision_attr_list(mrp_decision_conf_t *conf,
                               const char **buf, size_t len);
ssize_t mrp_decision_attr_value_list(mrp_decision_conf_t *conf,
                                     const char *attr_name,
                                     mrp_decision_attr_value_desc_t *buf,
                                     size_t len);

const char *mrp_decision_name(mrp_decision_conf_t *conf, int32_t decision);
int32_t mrp_decision_value_max(mrp_decision_conf_t *conf);

int32_t mrp_decision_get_integer_attr_value(mrp_decision_conf_t *conf,
                                            const char *attr_name,
                                            const char *value_name,
                                            bool *error);
const char *mrp_decision_get_integer_attr_name(mrp_decision_conf_t *conf,
                                               const char *attr_name,
                                               int32_t value,
                                               bool *error);

size_t mrp_decision_conf_print(mrp_decision_conf_t *conf,char *buf,size_t len);

mrp_decision_node_t *mrp_decision_tree_create_from_file(
                                               mrp_decision_conf_t *conf,
                                               const char *stem);

size_t mrp_decision_tree_print(mrp_decision_conf_t *conf,
                               mrp_decision_node_t *node,
                               char *buf, size_t len);

#endif /* __MRP_C5_DECISION_TREE_H__ */
