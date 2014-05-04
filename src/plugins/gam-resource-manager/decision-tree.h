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
#ifndef __MRP_DECISION_TREE_H__
#define __MRP_DECISION_TREE_H__

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#include <murphy/common/macros.h>

#define MRP_DECISION_BITMASK_WIDTH    (sizeof(mrp_decision_bitmask_t) * 8)
#define MRP_DECISION_BIT(b)           ((mrp_decision_bitmask_t)1 << (b))
#define MRP_DECISION_VALUE_BASIC(v)   ((v) & 0xff)

typedef enum mrp_decision_value_type_e       mrp_decision_value_type_t;
typedef enum mrp_decision_node_type_e        mrp_decision_node_type_t;
typedef enum mrp_decision_condition_e        mrp_decision_condition_t;

typedef union  mrp_decision_value_u          mrp_decision_value_t;
typedef struct mrp_decision_branch_s         mrp_decision_branch_t;
typedef struct mrp_decision_root_node_s      mrp_decision_root_node_t;
typedef struct mrp_decision_test_node_s      mrp_decision_test_node_t;
typedef struct mrp_decision_terminal_node_s  mrp_decision_terminal_node_t;
typedef union  mrp_decision_node_u           mrp_decision_node_t;
typedef union  mrp_decision_node_u           mrp_decision_tree_t;

typedef uint32_t mrp_decision_bitmask_t;

enum mrp_decision_value_type_e {
    MRP_DECISION_VALUE_UNKNOWN = -1,
    MRP_DECISION_VALUE_INTEGER = 0,
    MRP_DECISION_VALUE_UNSIGND,
    MRP_DECISION_VALUE_FLOATING,
    MRP_DECISION_VALUE_STRING,
    MRP_DECISION_VALUE_BASIC_TYPE_MAX,

    MRP_DECISION_VALUE_BITMASK = MRP_DECISION_VALUE_BASIC_TYPE_MAX,

    MRP_DECISION_ARRAY = 0x100,

    MRP_DECISION_ARRAY_INTEGER  = (MRP_DECISION_ARRAY |
                                   MRP_DECISION_VALUE_INTEGER),
    MRP_DECISION_ARRAY_UNSIGND  = (MRP_DECISION_ARRAY |
                                   MRP_DECISION_VALUE_UNSIGND),
    MRP_DECISION_ARRAY_FLOATING = (MRP_DECISION_ARRAY |
                                   MRP_DECISION_VALUE_FLOATING),
    MRP_DECISION_ARRAY_STRING   = (MRP_DECISION_ARRAY |
                                   MRP_DECISION_VALUE_STRING),
};

union mrp_decision_value_u {
    int32_t integer;
    uint32_t unsignd;
    double floating;
    mrp_decision_bitmask_t bitmask;
    const char *string;
    struct {
        size_t size;
        mrp_decision_value_t *values;
    } array;
};

enum mrp_decision_condition_e {
    MRP_DECISION_GT,
    MRP_DECISION_GE,
    MRP_DECISION_LE,
    MRP_DECISION_EQ,
    MRP_DECISION_LT,
    MRP_DECISION_IN,
};

struct mrp_decision_branch_s {
    mrp_decision_condition_t condition;
    int value_id;
    mrp_decision_value_type_t value_type;
    mrp_decision_value_t value;
    size_t offset;
    mrp_decision_node_t *node;
};

enum mrp_decision_node_type_e {
    MRP_DECISION_UNKNOWN_NODE = 0,
    MRP_DECISION_ROOT_NODE,
    MRP_DECISION_TEST_NODE,
    MRP_DECISION_TERMINAL_NODE
};

struct mrp_decision_root_node_s {
    mrp_decision_node_type_t type;
    const char *name;
    mrp_decision_value_type_t decision_value_type;
    mrp_decision_node_t *node;
};

struct mrp_decision_test_node_s {
    mrp_decision_node_type_t type;
    size_t nbranch;
    mrp_decision_branch_t *branches;
};

struct mrp_decision_terminal_node_s {
    mrp_decision_node_type_t type;
    mrp_decision_value_t decision;
};

union mrp_decision_node_u {
    mrp_decision_node_type_t type;
    mrp_decision_root_node_t root;
    mrp_decision_test_node_t test;
    mrp_decision_terminal_node_t terminal;
};


mrp_decision_tree_t *mrp_decision_tree_create(const char *name,
                                mrp_decision_value_type_t decision_value_type);
void mrp_decision_tree_destroy(mrp_decision_tree_t *tree);

mrp_decision_node_t *mrp_decision_create_terminal_node(
                                   mrp_decision_value_t *decision);
mrp_decision_node_t *mrp_decision_create_test_node(void);

bool mrp_decision_add_node_to_root(mrp_decision_tree_t *tree,
                                   mrp_decision_node_t *node);
bool mrp_decision_add_branch_to_test_node(mrp_decision_node_t *test_node,
                                          mrp_decision_condition_t condition,
                                          int value_id,
                                          mrp_decision_value_type_t value_type,
                                          mrp_decision_value_t *value,
                                          size_t offset,
                                          mrp_decision_node_t *node);


bool mrp_decision_make(mrp_decision_tree_t *tree, void *input,
                       mrp_decision_value_type_t *decision_value_type,
                       mrp_decision_value_t **decision_value);


const char *mrp_decision_value_type_str(mrp_decision_value_type_t type);
const char *mrp_decision_condition_str(mrp_decision_condition_t cond);

size_t mrp_decision_value_print(mrp_decision_value_type_t type,
                                mrp_decision_value_t *value,
                                char *buf, size_t len);


#endif  /* __MRP_DECISION_TREE_H__ */
