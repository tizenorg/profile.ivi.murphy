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

#ifndef __MURPHY_TELEPHONY_DBUS_UTILS_H__
#define __MURPHY_TELEPHONY_DBUS_UTILS_H__

#include <murphy/common/dbus-libdbus.h>


/**
 * Utility macros.
 */

#define FAIL_IF(cond, retval, err, args...) \
    do { if (MRP_UNLIKELY(cond)) { \
            if (MRP_LIKELY(err != NULL)) mrp_log_error(err, ## args); \
            return retval; \
       } } while(0)

#define FAIL_IF_NULL(ptr, retval, err, args...) \
    do { if (MRP_UNLIKELY((ptr) == NULL)) { \
            if (MRP_LIKELY(err != NULL)) mrp_log_error(err, ## args); \
            return retval; \
       } } while(0)


#define DUMP_STR(f)         ((f) ? (f)   : "")
#define DUMP_YESNO(f)       ((f) ? "yes" : "no")
#define FREE(p)             if((p) != NULL) mrp_free(p)


/**
 * Callback type for array foreach
 */
typedef void *(*array_cb_t) (mrp_dbus_msg_t *msg, void *user_data);


/**
 * Defines the specification for the parsing.
 * The implementation defines an array of this structure, denoting an object.
 */
typedef struct dict_spec {
    const char     *key;     /* property name */
    mrp_dbus_type_t type;    /* Murphy DBUS type */
    void           *valptr;  /* container for the value */
}mrp_dbus_dict_spec_t;

void free_strarr(char **strarr);
int strarr_contains(char **strarr, const char *str);
int dbus_array_foreach(mrp_dbus_msg_t *msg,
                       array_cb_t callback,
                       void *user_data);
int parse_dbus_string(mrp_dbus_msg_t *msg, char **target);
int parse_dbus_object_path(mrp_dbus_msg_t *msg, char **target);
bool parse_dbus_value(mrp_dbus_msg_t * msg,
                     mrp_dbus_dict_spec_t *spec_entry);
bool parse_dbus_dict_entry_tuple(mrp_dbus_msg_t *msg,
                                 mrp_dbus_dict_spec_t *spec,
                                 int size);
int parse_dbus_dict_entry(mrp_dbus_msg_t *msg, mrp_dbus_dict_spec_t *sp, int s);
int parse_dbus_dict(mrp_dbus_msg_t *msg,
                    array_cb_t eparser,
                    void *target);

#endif /* __MURPHY_TELEPHONY_DBUS_UTILS_H__ */
