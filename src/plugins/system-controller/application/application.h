/*
 * Copyright (c) 2013, Intel Corporation
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

#ifndef __MURPHY_APPLICATION_H__
#define __MURPHY_APPLICATION_H__

#include <sys/types.h>

#include "data-types.h"

#ifndef __MURPHY_WAYLAND_H__
#error "do not include directly application.h; include wayland/wayland.h"
#endif

#define MRP_APPLICATION_MAX 500
#define MRP_APPLICATION_BUCKETS (MRP_APPLICATION_MAX / 10)

typedef enum mrp_application_operation_e      mrp_application_operation_t;
typedef enum mrp_application_privilege_e      mrp_application_privilege_t;
typedef enum mrp_application_update_mask_e    mrp_application_update_mask_t;

typedef struct mrp_application_s              mrp_application_t;
typedef struct mrp_application_update_s       mrp_application_update_t;
typedef struct mrp_application_privileges_s   mrp_application_privileges_t;

enum mrp_application_operation_e {
    MRP_APPLICATION_OPERATION_NONE = 0,
    MRP_APPLICATION_CREATE,
    MRP_APPLICATION_DESTROY,
};

enum mrp_application_privilege_e {
    MRP_APPLICATION_PRIVILEGE_NONE =           0,
    MRP_APPLICATION_PRIVILEGE_CERTIFIED,    /* 1 */
    MRP_APPLICATION_PRIVILEGE_MANUFACTURER, /* 2 */
    MRP_APPLICATION_PRIVILEGE_SYSTEM,       /* 3 */
    MRP_APPLICATION_PRIVILEGE_UNLIMITED,    /* 4 */

    MRP_APPLICATION_PRIVILEGE_MAX           /* 5 */
};

struct mrp_application_privileges_s {
    mrp_application_privilege_t screen;
    mrp_application_privilege_t audio;
};

struct mrp_application_s {
    char *appid;
    char *area_name;

    mrp_wayland_area_t *area;
    mrp_application_privileges_t privileges;
    const char *resource_class;
    int32_t screen_priority;

    void *scripting_data;
};

enum mrp_application_update_mask_e {
    MRP_APPLICATION_APPID_MASK      = 0x01,
    MRP_APPLICATION_AREA_NAME_MASK  = 0x02,
    MRP_APPLICATION_AREA_MASK       = 0x04,
    MRP_APPLICATION_SCREEN_PRIVILEGE_MASK = 0x08,
    MRP_APPLICATION_AUDIO_PRIVILEGE_MASK  = 0x10,
    MRP_APPLICATION_PRIVILEGES_MASK       = 0x18,
    MRP_APPLICATION_RESOURCE_CLASS_MASK   = 0x20,
    MRP_APPLICATION_SCREEN_PRIORITY_MASK  = 0x40,

    MRP_APPLICATION_END_MASK        = 0x80
};

struct mrp_application_update_s {
    mrp_application_update_mask_t mask;    
    const char *appid;
    const char *area_name;
    const char *resource_class;
    mrp_application_privileges_t privileges;
    int32_t screen_priority;
};

mrp_application_t *mrp_application_create(mrp_application_update_t *u,
                                          void *scripting_data);
void mrp_application_destroy(mrp_application_t *app);

mrp_application_t *mrp_application_find(const char *appid);

size_t mrp_application_print(mrp_application_t *app,
                             mrp_application_update_mask_t mask,
                             char *buf, size_t len);

void mrp_application_set_scripting_data(mrp_application_t *app, void *data);

int mrp_application_foreach(mrp_htbl_iter_cb_t cb, void *user_data);

const char *mrp_application_privilege_str(mrp_application_privilege_t priv);

#endif /* __MURPHY_APPLICATION_H__ */
