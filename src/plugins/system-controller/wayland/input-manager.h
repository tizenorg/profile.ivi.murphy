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

#ifndef __MURPHY_WAYLAND_INPUT_MANAGER_H__
#define __MURPHY_WAYLAND_INPUT_MANAGER_H__

#include <sys/types.h>

#include "wayland/wayland.h"

enum mrp_wayland_input_manager_operation_e {
    MRP_WAYLAND_INPUT_MANAGER_OPERATION_NONE = 0,
    MRP_WAYLAND_INPUT_MANAGER_CREATE,       /* 1 */
    MRP_WAYLAND_INPUT_MANAGER_DESTROY,      /* 2 */
    MRP_WAYLAND_INPUT_MANAGER_READY,        /* 3 */
};

#define MRP_WAYLAND_INPUT_MANAGER_COMMON                                \
    MRP_WAYLAND_OBJECT_COMMON;                                          \
    void (*input_request)(mrp_wayland_input_t *,                        \
                          mrp_wayland_input_update_t *);                \
    void (*send_input)(mrp_wayland_code_t *, int32_t, uint32_t, int32_t)


struct mrp_wayland_input_manager_s {
    MRP_WAYLAND_INPUT_MANAGER_COMMON;
};

#endif /* __MURPHY_WAYLAND_INPUT_MANAGER_H__ */
