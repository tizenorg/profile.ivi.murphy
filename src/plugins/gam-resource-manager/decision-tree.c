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
#include <murphy/common.h>
#include <breedline/breedline-murphy.h>

#include "decision-tree.h"


static mrp_decision_value_t *traverse_tree(mrp_decision_node_t *, void *);
static bool test_condition(mrp_decision_branch_t *, void *);

static void copy_value(mrp_decision_value_type_t, mrp_decision_value_t *,
                       mrp_decision_value_t *);
static void destroy_value(mrp_decision_value_type_t,
                          mrp_decision_value_t *);


mrp_decision_tree_t *mrp_decision_tree_create(const char *name,
                                mrp_decision_value_type_t decision_value_type)
{
    mrp_decision_node_t *tree;

    if (!name || decision_value_type < 0 ||
        decision_value_type >= MRP_DECISION_VALUE_BASIC_TYPE_MAX)
    {
        return NULL;
    }

    if ((tree = mrp_allocz(sizeof(mrp_decision_root_node_t)))) {
        tree->root.type = MRP_DECISION_ROOT_NODE;
        tree->root.name = mrp_strdup(name);
        tree->root.decision_value_type = decision_value_type;
        tree->root.node = NULL;
    }

    return tree;
}


void mrp_decision_tree_destroy(mrp_decision_tree_t *node)
{
    mrp_decision_branch_t *branch;
    size_t i;

    if (node) {
        switch (node->type) {

        case MRP_DECISION_ROOT_NODE:
            mrp_decision_tree_destroy(node->root.node);
            mrp_free((void *)node->root.name);
            break;

        case MRP_DECISION_TEST_NODE:
            for (i = 0;  i < node->test.nbranch;  i++) {
                branch = node->test.branches + i;

                destroy_value(branch->value_type, &branch->value);
                mrp_decision_tree_destroy(branch->node);
            }
            mrp_free(node->test.branches);
            break;

        case MRP_DECISION_TERMINAL_NODE:
            break;

        default:
            return;
        }

        mrp_free(node);
    }
}

mrp_decision_node_t *
mrp_decision_create_terminal_node(mrp_decision_value_t *decision)
{
    mrp_decision_node_t *node;

    if (!decision)
        return NULL;

    if ((node = mrp_allocz(sizeof(mrp_decision_terminal_node_t)))) {
        node->terminal.type = MRP_DECISION_TERMINAL_NODE;
        node->terminal.decision = *decision;
    }

    return node;
}


mrp_decision_node_t *mrp_decision_create_test_node(void)
{
    mrp_decision_node_t *node;

    if ((node = mrp_allocz(sizeof(mrp_decision_test_node_t)))) {
        node->terminal.type = MRP_DECISION_TEST_NODE;
    }

    return node;
}

bool mrp_decision_add_node_to_root(mrp_decision_node_t *root,
                                   mrp_decision_node_t *node)
{
    if (!root || !node)
        return false;

    if (root->type != MRP_DECISION_ROOT_NODE || root->root.node)
        return false;

    root->root.node = node;

    return true;
}

bool mrp_decision_add_branch_to_test_node(mrp_decision_node_t *test_node,
                                          mrp_decision_condition_t condition,
                                          int value_id,
                                          mrp_decision_value_type_t value_type,
                                          mrp_decision_value_t *value,
                                          size_t offset,
                                          mrp_decision_node_t *node)
{
    mrp_decision_branch_t *branch;
    size_t size;
    size_t i;

    if (!test_node || test_node->type != MRP_DECISION_TEST_NODE ||
        !value || !node)
    {
        return false;
    }

    i = test_node->test.nbranch++;
    size = sizeof(mrp_decision_branch_t) * test_node->test.nbranch;
    test_node->test.branches = mrp_realloc(test_node->test.branches, size);

    branch = test_node->test.branches + i;
    memset(branch, 0, sizeof(mrp_decision_branch_t));

    branch->condition = condition;
    branch->value_id = value_id;
    branch->value_type = value_type;
    branch->offset = offset;
    branch->node = node;

    copy_value(value_type, value, &branch->value);

    return true;
}



bool mrp_decision_make(mrp_decision_tree_t *node, void *input,
                       mrp_decision_value_type_t *decision_value_type,
                       mrp_decision_value_t **decision_value)
{
    if (!node || node->type != MRP_DECISION_ROOT_NODE ||
        !input || !decision_value)
    {
        return false;
    }

    if (decision_value_type)
        *decision_value_type = MRP_DECISION_VALUE_UNKNOWN;

    if ((*decision_value = traverse_tree(node->root.node, input))) {
        if (decision_value_type)
            *decision_value_type = node->root.decision_value_type;

        return true;
    }

    return false;
}


const char *mrp_decision_value_type_str(mrp_decision_value_type_t type)
{
    switch (type) {
    case MRP_DECISION_VALUE_INTEGER:
        return "integer";
    case MRP_DECISION_VALUE_UNSIGND:
        return "unsigned";
    case MRP_DECISION_VALUE_FLOATING:
        return "floating";
    case MRP_DECISION_VALUE_STRING:
        return "string";
    case MRP_DECISION_VALUE_BITMASK:
        return "bitmask";
    case MRP_DECISION_ARRAY_INTEGER:
        return "integer array";
    case MRP_DECISION_ARRAY_UNSIGND:
        return "unsigned array";
    case MRP_DECISION_ARRAY_FLOATING:
        return "floating array";
    case MRP_DECISION_ARRAY_STRING:
        return "string array";
    default:
        return "<unknown value type>";
    }
}

const char *mrp_decision_condition_str(mrp_decision_condition_t cond)
{
    switch (cond) {
    case MRP_DECISION_GT:   return ">";
    case MRP_DECISION_GE:   return ">=";
    case MRP_DECISION_EQ:   return "=";
    case MRP_DECISION_LE:   return "<=";
    case MRP_DECISION_LT:   return "<";
    case MRP_DECISION_IN:   return "in";
    default:                return "<unknown condition>";
    }
}

size_t mrp_decision_value_print(mrp_decision_value_type_t type,
                                mrp_decision_value_t *value,
                                char *buf, size_t len)
{
#define ELLIPSIS        " ... "
#define PRINT(args...)  do {if (p<h) p += snprintf(p, h-p, args);    } while(0)
#define PRINT_ELLIPSIS  do {if (p<e) p += snprintf(p, e-p, ELLIPSIS);} while(0)
#define PRINT_LISTSTART do {if (p<e) p += snprintf(p, e-p, "[");     } while(0)
#define PRINT_LISTEND   do {if (p<e) p += snprintf(p, e-p, "]");     } while(0)


    mrp_decision_value_type_t basic_type;
    mrp_decision_value_t *v;
    char *p, *e, *h;
    size_t i, n;

    if (len < sizeof(ELLIPSIS) + 3)
        return 0;

    h = (e = (p = buf) + len) - (sizeof(ELLIPSIS) + 1);

    if (!(type & MRP_DECISION_ARRAY)) {
        switch (type) {
        case MRP_DECISION_VALUE_INTEGER:  PRINT("%d",  value->integer ); break;
        case MRP_DECISION_VALUE_UNSIGND:  PRINT("%u",  value->unsignd ); break;
        case MRP_DECISION_VALUE_FLOATING: PRINT("%lf", value->floating); break;
        case MRP_DECISION_VALUE_STRING:   PRINT("'%s'",value->string  ); break;
        case MRP_DECISION_VALUE_BITMASK:  PRINT("0x%x",value->bitmask ); break;
        default:                          PRINT("<unknown value type>"); break;
        }
    }
    else {
        basic_type = MRP_DECISION_VALUE_BASIC(type);
        n = value->array.size;
        v = value->array.values;

        PRINT_LISTSTART;

        for (i = 0;   i < n;   i++) {
            if (p >= h) {
                PRINT_ELLIPSIS;
                break;
            }

            if (i > 0)
                PRINT(", ");

            p += mrp_decision_value_print(basic_type, v+i, p, e-p);
        }

        PRINT_LISTEND;
    }

    return p - buf;

#undef PRINT_LISTEND
#undef PRINT_LISTSTART
#undef PRINT_ELLIPSIS
#undef PRINT
#undef ELLIPSIS
}


static mrp_decision_value_t *traverse_tree(mrp_decision_node_t *node,
                                           void *input)
{
    mrp_decision_branch_t *branch;
    size_t i, n;

    if (node && input) {
        switch (node->type) {

        case MRP_DECISION_TERMINAL_NODE:
            return &node->terminal.decision;
            break;

        case MRP_DECISION_TEST_NODE:
            for (i = 0, n = node->test.nbranch, branch = node->test.branches;
                 i < n;
                 i++, branch++)
            {
                if (test_condition(branch, input))
                    return traverse_tree(branch->node, input);
            }
            return NULL;

        default:
            return NULL;
        }
    }

    return NULL;
}


static bool test_condition(mrp_decision_branch_t *branch, void *input)
{
#define INPUT_VALUE(_t) (*(_t *)(((char *)input) + branch->offset))

    uint32_t bitidx;
    mrp_decision_bitmask_t bit;
    mrp_decision_value_t *value;
    int32_t integer;
    uint32_t unsignd;
    double floating;
    const char *string;
    size_t i;

    if (!branch || !input)
        return false;

    switch (branch->condition) {

    case MRP_DECISION_GT:
        switch (branch->value_type) {
        case MRP_DECISION_VALUE_INTEGER:
            return INPUT_VALUE(int32_t) > branch->value.integer;
        case MRP_DECISION_VALUE_UNSIGND:
            return INPUT_VALUE(uint32_t) > branch->value.unsignd;
        case MRP_DECISION_VALUE_FLOATING:
            return INPUT_VALUE(double) > branch->value.floating;
        default:
            return false;
        }

    case MRP_DECISION_GE:
        switch (branch->value_type) {
        case MRP_DECISION_VALUE_INTEGER:
            return INPUT_VALUE(int32_t) >= branch->value.integer;
        case MRP_DECISION_VALUE_UNSIGND:
            return INPUT_VALUE(uint32_t) >= branch->value.unsignd;
        case MRP_DECISION_VALUE_FLOATING:
            return INPUT_VALUE(double) >= branch->value.floating;
        default:
            return false;
        }

    case MRP_DECISION_EQ:
        switch (branch->value_type) {
        case MRP_DECISION_VALUE_INTEGER:
            return INPUT_VALUE(int32_t) == branch->value.integer;
        case MRP_DECISION_VALUE_UNSIGND:
            return INPUT_VALUE(uint32_t) == branch->value.unsignd;
        case MRP_DECISION_VALUE_FLOATING:
            return INPUT_VALUE(double) == branch->value.floating;
        case MRP_DECISION_VALUE_STRING:
            return strcmp(INPUT_VALUE(char *), branch->value.string);
        default:
            return false;
        }

    case MRP_DECISION_LE:
        switch (branch->value_type) {
        case MRP_DECISION_VALUE_INTEGER:
            return INPUT_VALUE(int32_t) <= branch->value.integer;
        case MRP_DECISION_VALUE_UNSIGND:
            return INPUT_VALUE(uint32_t) <= branch->value.unsignd;
        case MRP_DECISION_VALUE_FLOATING:
            return INPUT_VALUE(double) <= branch->value.floating;
        default:
            return false;
        }

    case MRP_DECISION_LT:
        switch (branch->value_type) {
        case MRP_DECISION_VALUE_INTEGER:
            return INPUT_VALUE(int32_t) < branch->value.integer;
        case MRP_DECISION_VALUE_UNSIGND:
            return INPUT_VALUE(uint32_t) < branch->value.unsignd;
        case MRP_DECISION_VALUE_FLOATING:
            return INPUT_VALUE(double) < branch->value.floating;
        default:
            return false;
        }

    case MRP_DECISION_IN:
        if ((branch->value_type & MRP_DECISION_ARRAY)) {

            integer = 0;
            unsignd = 0;
            floating = 0;
            string = "";

            switch ((branch->value_type & 0xff)) {
            case MRP_DECISION_VALUE_INTEGER:
                integer = INPUT_VALUE(int32_t);
                break;
            case MRP_DECISION_VALUE_UNSIGND:
                unsignd = INPUT_VALUE(uint32_t);
                break;
            case MRP_DECISION_VALUE_FLOATING:
                floating = INPUT_VALUE(double);
                break;
            case MRP_DECISION_VALUE_STRING:
                string = INPUT_VALUE(char *);
                break;
            default:
                return false;
            }
            for (i = 0;    i < branch->value.array.size;    i++) {
                value = branch->value.array.values + i;
                switch ((branch->value_type & 0xff)) {
                case MRP_DECISION_VALUE_INTEGER:
                    if (integer == value->integer)
                        return true;
                    break;
                case MRP_DECISION_VALUE_UNSIGND:
                    if (unsignd == value->unsignd)
                        return true;
                    break;
                case MRP_DECISION_VALUE_FLOATING:
                    if (floating == value->floating)
                        return true;
                    break;
                case MRP_DECISION_VALUE_STRING:
                    if (!strcmp(string, value->string))
                        return true;
                    break;
                default:
                    return false;
                }
            } /* for i */
            return false;
        }
        else {
            if (branch->value_type == MRP_DECISION_VALUE_BITMASK) {
                bitidx = INPUT_VALUE(int32_t);

                if (bitidx >= MRP_DECISION_BITMASK_WIDTH)
                    return false;

                bit = MRP_DECISION_BIT(bitidx);

                return (bit & branch->value.bitmask) ? true : false;
            }
        }
        return false;
    }

    return false;
}

static void copy_value(mrp_decision_value_type_t type,
                       mrp_decision_value_t *src,
                       mrp_decision_value_t *dst)
{
    mrp_decision_value_type_t basic_type;
    mrp_decision_value_t *src_values;
    mrp_decision_value_t *dst_values;
    size_t i, n;

    if (!(type & MRP_DECISION_ARRAY)) {
        if (type == MRP_DECISION_VALUE_STRING)
            dst->string = mrp_strdup(src->string);
        else
            *dst = *src;
    }
    else {
        n = src->array.size;

        dst->array.size = n;
        dst->array.values = mrp_allocz(sizeof(dst->array.values[0]) * n);

        basic_type = MRP_DECISION_VALUE_BASIC(type);
        src_values = src->array.values;
        dst_values = dst->array.values;

        for (i = 0;  i < n;  i++)
            copy_value(basic_type, src_values + i, dst_values + i);
    }
}


static void destroy_value(mrp_decision_value_type_t type,
                          mrp_decision_value_t *value)
{
    mrp_decision_value_type_t basic_type;
    size_t i;

    if (!(type & MRP_DECISION_ARRAY)) {
        if (type == MRP_DECISION_VALUE_STRING)
            mrp_free((void *)value->string);
    }
    else {
        basic_type = MRP_DECISION_VALUE_BASIC(type);

        for (i = 0;   i < value->array.size;   i++)
            destroy_value(basic_type, value->array.values + i);
    }
}
