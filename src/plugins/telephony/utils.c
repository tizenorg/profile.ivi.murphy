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


#include <stdarg.h>
#include <string.h>
#include <murphy/common.h>
#include <murphy/core.h>
#include "utils.h"


void free_strarr(char **strarr)
{
    int n;

    if (strarr != NULL) {
        for (n = 0; strarr[n] != NULL; n++)
            mrp_free(strarr[n]);

        mrp_free(strarr);
    }
}


int strarr_contains(char **strarr, const char *str)
{
    int n;

    if (strarr != NULL)
        for (n = 0; strarr[n] != NULL; n++)
            if (!strcmp(strarr[n], str))
                return TRUE;

    return FALSE;
}


int dbus_array_foreach(mrp_dbus_msg_t *msg,
                       array_cb_t callback,
                       void *user_data)
{
    int ret = TRUE;

    FAIL_IF(mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_ARRAY, FALSE, \
            "attempted to call dbus_array_foreach on non-array");

    mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_ARRAY, NULL);

    while (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_INVALID) {
        if (!callback(msg, user_data)) {
            /* yes, iterating is done inside the callbacks by reading the
             * values using mrp_dbus_msg_read_X functions... but all possible
             * callbacks are defined in the implementation. We have to make
             * sure that all of them will iterate, to avoid infinite loop. */
            ret = FALSE;
            break;
        }
    }

    mrp_dbus_msg_exit_container(msg);

    return ret;
}


inline int parse_dbus_string(mrp_dbus_msg_t *msg, char **target)
{
    FAIL_IF(mrp_dbus_msg_arg_type(msg, NULL) != DBUS_TYPE_STRING, FALSE, \
            "parse_dbus_string: expected string");

    return mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, (void *) target);
}


inline int parse_dbus_object_path(mrp_dbus_msg_t *msg, char **target)
{
    FAIL_IF(mrp_dbus_msg_arg_type(msg, NULL) != DBUS_TYPE_OBJECT_PATH, \
            FALSE, "parse_dbus_object_path: expected object path");

    return mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_OBJECT_PATH,
                                   (void *)target);
}


/**
 * Match a key against the specified property, and if matched, fetch the value.
 * This is not a generic parser, but simplified for typical telephony data,
 * where a value is either a basic type, or array of strings.
 */
bool parse_dbus_value(mrp_dbus_msg_t * msg,
                      mrp_dbus_dict_spec_t * spe)
{
    const char * sig;

    if (spe->type == MRP_DBUS_TYPE_STRING ||
        spe->type == MRP_DBUS_TYPE_OBJECT_PATH) {

        mrp_free(*(char **)(spe->valptr));
    }

    if (spe->type == MRP_DBUS_TYPE_ARRAY) /* array of strings expected */
    {
#if 1
        size_t  i, n;
        char ** strarr, **copy;

        if(!mrp_dbus_msg_read_array(msg, MRP_DBUS_TYPE_STRING,
                                    (void**)&strarr, &n)) {
            mrp_log_error("failed to read string array for property %s",
                          spe->key);
            return FALSE;
        }

        if((copy = mrp_alloc((n + 1) * sizeof(char*)))) {
            copy[n] = NULL;
            for(i = 0; i < n; i++) {
                copy[i] = mrp_strdup(strarr[i]);
                /*mrp_debug("....parsed stringarray[%d]= %s", i, copy[i]);*/
            }
            *((char***)spe->valptr) = copy;
        }

        mrp_debug("Parsed string array of size %zu", n);
#else
        int n = 1;
        char ** ap = NULL;

        mrp_free(*(char **)(spe->valptr));
        mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_ARRAY, NULL);

        while(mrp_dbus_msg_arg_type(msg, &sig) == MRP_DBUS_TYPE_STRING) {
            if(!mrp_dbus_msg_read_basic(msg, spe->type, ap)) {
                char *** strarr = spe->valptr;
                if (mrp_reallocz(strarr, n, n + 1) != NULL) {
                    if ((strarr[n-1] = mrp_strdup(str)) == NULL) {
                        free_strarr(strarr);
                        return FALSE;
                    }
                    n++;
                }
            }
        }
        mrp_dbus_msg_exit_container(msg);
#endif
    } else { /* basic types come here */
        FAIL_IF(mrp_dbus_msg_arg_type(msg, &sig) != spe->type, 0,
                "wrong type '%s' for key %s: expected '%c'",
                sig, spe->key, spe->type);

        FAIL_IF(!mrp_dbus_msg_read_basic(msg, spe->type, spe->valptr), 0,
                "failed to read value for property %s", spe->key);

        if (spe->type == MRP_DBUS_TYPE_STRING ||
                spe->type == MRP_DBUS_TYPE_OBJECT_PATH) {

            *(char **)(spe->valptr) = mrp_strdup(*(char **)(spe->valptr));

            if (*(char **)(spe->valptr) == NULL) {
                mrp_log_error("failed to allocate property %s", spe->key);
                return FALSE;
            }
        }
    }

    return TRUE;
}


/* parse a key and a value, with the pointer set to the key */
bool parse_dbus_dict_entry_tuple(mrp_dbus_msg_t *msg,
                                 mrp_dbus_dict_spec_t * spec, int spec_size)
{
    const char *key, *sig;
    int         i;
    bool        retval = FALSE;

    FAIL_IF(spec_size <= 0, FALSE, "called with invalid spec size, ");

    /* expect a pointer set to the property key as string */
    FAIL_IF(mrp_dbus_msg_arg_type(msg, &sig) != MRP_DBUS_TYPE_STRING, \
            FALSE, "wrong key type %s, expected string", sig);

    mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, &key);
    mrp_debug("read key %s", key);

    FAIL_IF(mrp_dbus_msg_arg_type(msg, &sig) != DBUS_TYPE_VARIANT, \
            FALSE, "wrong type %s for key %s: expected variant", sig, key);

    mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_VARIANT, NULL);

    /*
     * there will be maximum 1 match for the given key
     * error is returned only if a field is matched, but value fetching fails
     */
    for (i = 0; i < spec_size; i++)
        if (!strcmp(key, spec[i].key)) {
            mrp_debug("matched property %s", spec[i].key);
            if(parse_dbus_value(msg, spec+i))
                retval = TRUE;
            break;
        }

    mrp_dbus_msg_exit_container(msg);

    return retval;
}


/** parse a dictionary entry as {key, variant} */
int parse_dbus_dict_entry(mrp_dbus_msg_t *msg,
                          mrp_dbus_dict_spec_t *spec, int size)
{
    mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_DICT_ENTRY, NULL);

    int ret = parse_dbus_dict_entry_tuple(msg, spec, size);

    mrp_dbus_msg_exit_container(msg);

    return ret;
}


/** parse a dictionary as an array of {key, variant} tuples */
int parse_dbus_dict(mrp_dbus_msg_t *msg,
                    array_cb_t      eparser,
                    void           *target)
{
    FAIL_IF(mrp_dbus_msg_arg_type(msg, NULL) != DBUS_TYPE_ARRAY, FALSE, \
            "malformed dictionary: not array");

    FAIL_IF(!dbus_array_foreach(msg, eparser, target), FALSE, \
            "failed to parse properties");

    return TRUE;
}
