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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <murphy/common.h>

#include <murphy/resource/manager-api.h>

#include "class.h"


mrp_resmgr_class_t *mrp_resmgr_class_create(mrp_list_hook_t *classes,
                                            mrp_application_class_t *ac)
{
    mrp_list_hook_t *insert_before, *entry, *n;
    mrp_resmgr_class_t *class, *c;
    uint32_t pri;

    if (ac) {
        pri = mrp_application_class_get_priority(ac);

        if ((class = mrp_allocz(sizeof(mrp_resmgr_class_t)))) {
            class->class = ac;
            mrp_list_init(&class->resources);

            insert_before = classes;
            mrp_list_foreach_back(classes, entry, n) {
                c = mrp_list_entry(entry, mrp_resmgr_class_t, link);

                if (pri >= mrp_application_class_get_priority(c->class))
                    break;

                insert_before = entry;
            }

            mrp_list_insert_before(insert_before, &class->link);

            return class;
        }
    }

    return NULL;
}


void mrp_resmgr_class_destroy(mrp_resmgr_class_t *rc)
{
    if (rc) {
        mrp_list_delete(&rc->link);
        mrp_free(rc);
    }
}


mrp_resmgr_class_t *mrp_resmgr_class_find(mrp_list_hook_t *classes,
                                          mrp_application_class_t *ac)
{
    mrp_list_hook_t *entry, *n;
    mrp_resmgr_class_t *rc;

    if (ac) {
        mrp_list_foreach(classes, entry, n) {
            rc = mrp_list_entry(entry, mrp_resmgr_class_t, link);

            if (ac == rc->class)
                return rc;
        }
    }

    return NULL;
}






/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
