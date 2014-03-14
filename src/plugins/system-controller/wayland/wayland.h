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

#ifndef __MURPHY_WAYLAND_H__
#define __MURPHY_WAYLAND_H__

#include <wayland-client.h>

#include <murphy/common.h>

#include "data-types.h"

#define MRP_WAYLAND_INTERFACE_MAX        64
#define MRP_WAYLAND_OUTPUT_MAX           16
#define MRP_WAYLAND_WINDOW_MAX           256
#define MRP_WAYLAND_LAYER_MAX            32
#define MRP_WAYLAND_LAYER_BUILTIN        8
#define MRP_WAYLAND_AREA_MAX             32
#define MRP_WAYLAND_DEVICE_MAX           64
#define MRP_WAYLAND_INPUT_MAX            128
#define MRP_WAYLAND_CODE_MAX             512
#define MRP_WAYLAND_FRAMERATE_MAX        100

#define MRP_WAYLAND_INTERFACE_BUCKETS    (MRP_WAYLAND_INTERFACE_MAX / 2)
#define MRP_WAYLAND_OUTPUT_BUCKETS       (MRP_WAYLAND_OUTPUT_MAX / 4)
#define MRP_WAYLAND_WINDOW_BUCKETS       (MRP_WAYLAND_WINDOW_MAX / 2)
#define MRP_WAYLAND_LAYER_BUCKETS        ((MRP_WAYLAND_LAYER_MAX + \
                                           MRP_WAYLAND_LAYER_BUILTIN)/ 4)
#define MRP_WAYLAND_AREA_BUCKETS         (MRP_WAYLAND_AREA_MAX / 4)
#define MRP_WAYLAND_DEVICE_BUCKETS       (MRP_WAYLAND_DEVICE_MAX / 4)
#define MRP_WAYLAND_INPUT_BUCKETS        (MRP_WAYLAND_INPUT_MAX / 8)
#define MRP_WAYLAND_CODE_BUCKETS         (MRP_WAYLAND_INPUT_MAX / 16)

#define MRP_WAYLAND_NO_UPDATE            INT32_MIN

#define MRP_WAYLAND_OBJECT_COMMON               \
    mrp_list_hook_t interface_link;             \
    mrp_wayland_interface_t *interface;         \
    uint32_t name;                              \
    uint32_t version;                           \
    struct wl_proxy *proxy


typedef enum mrp_sysctl_scripting_field_e     mrp_wayland_scripting_field_t;
typedef enum mrp_wayland_output_operation_e   mrp_wayland_output_operation_t;
typedef enum mrp_wayland_output_update_mask_e mrp_wayland_output_update_mask_t;
typedef enum mrp_wayland_layer_type_e         mrp_wayland_layer_type_t;
typedef enum mrp_wayland_layer_operation_e    mrp_wayland_layer_operation_t;
typedef enum mrp_wayland_layer_update_mask_e  mrp_wayland_layer_update_mask_t;
typedef enum mrp_wayland_window_update_mask_e mrp_wayland_window_update_mask_t;
typedef enum mrp_wayland_window_operation_e   mrp_wayland_window_operation_t;
typedef enum mrp_wayland_animation_type_e     mrp_wayland_animation_type_t;
typedef enum mrp_wayland_active_e             mrp_wayland_active_t;
typedef enum mrp_wayland_area_operation_e     mrp_wayland_area_operation_t;
typedef enum mrp_wayland_area_align_e         mrp_wayland_area_align_t;
typedef enum mrp_wayland_area_update_mask_e   mrp_wayland_area_update_mask_t;
typedef enum mrp_wayland_window_manager_operation_e
                                        mrp_wayland_window_manager_operation_t;
typedef enum mrp_wayland_input_operation_e    mrp_wayland_input_operation_t;
typedef enum mrp_wayland_input_type_e         mrp_wayland_input_type_t;
typedef enum mrp_wayland_input_update_mask_e  mrp_wayland_input_update_mask_t;
typedef enum mrp_wayland_code_operation_e     mrp_wayland_code_operation_t;
typedef enum mrp_wayland_code_update_mask_e   mrp_wayland_code_update_mask_t;
typedef enum mrp_wayland_input_manager_operation_e
                                        mrp_wayland_input_manager_operation_t;

typedef struct mrp_wayland_s                  mrp_wayland_t;
typedef struct mrp_wayland_factory_s          mrp_wayland_factory_t;
typedef struct mrp_wayland_interface_s        mrp_wayland_interface_t;
typedef struct mrp_wayland_object_s           mrp_wayland_object_t;
typedef struct mrp_wayland_animation_s        mrp_wayland_animation_t;
typedef struct mrp_wayland_output_s           mrp_wayland_output_t;
typedef struct mrp_wayland_output_update_s    mrp_wayland_output_update_t;
typedef struct mrp_wayland_layer_s            mrp_wayland_layer_t;
typedef struct mrp_wayland_layer_update_s     mrp_wayland_layer_update_t;
typedef struct mrp_wayland_window_s           mrp_wayland_window_t;
typedef struct mrp_wayland_window_map_s       mrp_wayland_window_map_t;
typedef struct mrp_wayland_window_update_s    mrp_wayland_window_update_t;
typedef struct mrp_wayland_area_s             mrp_wayland_area_t;
typedef struct mrp_wayland_area_update_s      mrp_wayland_area_update_t;
typedef struct mrp_wayland_window_manager_s   mrp_wayland_window_manager_t;
typedef struct mrp_wayland_input_manager_s    mrp_wayland_input_manager_t;
typedef struct mrp_wayland_input_device_s     mrp_wayland_input_device_t;
typedef struct mrp_wayland_code_s             mrp_wayland_code_t;
typedef struct mrp_wayland_code_update_s      mrp_wayland_code_update_t;
typedef struct mrp_wayland_input_s            mrp_wayland_input_t;
typedef struct mrp_wayland_input_update_s     mrp_wayland_input_update_t;
typedef struct mrp_wayland_input_event_s      mrp_wayland_input_event_t;

#include "application/application.h"

typedef bool (*mrp_wayland_constructor_t)(mrp_wayland_t *,
                                          mrp_wayland_object_t *);
typedef void (*mrp_wayland_destructor_t)(mrp_wayland_object_t *);

typedef void (*mrp_wayland_window_manager_update_callback_t)(mrp_wayland_t *,
                                        mrp_wayland_window_manager_operation_t,
                                        mrp_wayland_window_manager_t *);
typedef void (*mrp_wayland_output_update_callback_t)(mrp_wayland_t *,
                                        mrp_wayland_output_operation_t,
                                        mrp_wayland_output_update_mask_t,
                                        mrp_wayland_output_t *);
typedef void (*mrp_wayland_layer_update_callback_t)(mrp_wayland_t *,
                                        mrp_wayland_layer_operation_t,
                                        mrp_wayland_layer_update_mask_t,
                                        mrp_wayland_layer_t *);
typedef void (*mrp_wayland_area_update_callback_t)(mrp_wayland_t *,
                                        mrp_wayland_area_operation_t,
                                        mrp_wayland_area_update_mask_t,
                                        mrp_wayland_area_t *);
typedef void (*mrp_wayland_window_update_callback_t)(mrp_wayland_t *,
                                        mrp_wayland_window_operation_t,
                                        mrp_wayland_window_update_mask_t,
                                        mrp_wayland_window_t *);
typedef void (*mrp_wayland_window_hint_callback_t)(mrp_wayland_t *,
                                        mrp_wayland_window_operation_t,
                                        mrp_wayland_window_update_t *);
typedef void (*mrp_wayland_app_update_callback_t)(mrp_wayland_t *,
                                        mrp_application_operation_t,
                                        mrp_application_update_mask_t,
                                        mrp_application_t *);

typedef void (*mrp_wayland_input_manager_update_callback_t)(mrp_wayland_t *,
                                        mrp_wayland_input_manager_operation_t,
                                        mrp_wayland_input_manager_t *);
typedef void (*mrp_wayland_input_update_callback_t)(mrp_wayland_t *,
                                        mrp_wayland_input_operation_t,
                                        mrp_wayland_input_update_mask_t,
                                        mrp_wayland_input_t *);
typedef void (*mrp_wayland_code_update_callback_t)(mrp_wayland_t *,
                                        mrp_wayland_code_operation_t,
                                        mrp_wayland_code_update_mask_t,
                                        mrp_wayland_code_t *);

struct mrp_wayland_s {
    const char *display_name;

    mrp_mainloop_t *ml;
    mrp_io_watch_t *iow;

    struct wl_display *display;
    struct wl_registry *registry;

    struct wl_registry_listener registry_listener;

    mrp_htbl_t *interfaces;
    mrp_htbl_t *outputs;
    mrp_htbl_t *windows;
    mrp_htbl_t *areas;

    struct {
        mrp_htbl_t *by_id;
        mrp_htbl_t *by_name;
    } devices; /* input devices more precisely */

    struct {
        mrp_htbl_t *by_id;
        mrp_htbl_t *by_type;
    } layers;


    mrp_wayland_window_manager_t *wm;
    mrp_wayland_input_manager_t *im;

    mrp_wayland_window_manager_update_callback_t window_manager_update_callback;
    mrp_wayland_input_manager_update_callback_t input_manager_update_callback;

    mrp_wayland_output_update_callback_t output_update_callback;
    mrp_wayland_window_update_callback_t window_update_callback;
    mrp_wayland_window_hint_callback_t window_hint_callback;
    mrp_wayland_layer_update_callback_t layer_update_callback;
    mrp_wayland_area_update_callback_t area_update_callback;
    mrp_wayland_app_update_callback_t application_update_callback;
    void *scripting_window_data;

    mrp_wayland_input_update_callback_t input_update_callback;
    mrp_wayland_code_update_callback_t code_update_callback;
    void *scripting_input_data;

    bool create_scripting_windows;
    bool create_scripting_outputs;
    bool create_scripting_areas;
    bool create_scripting_layers;

    bool create_scripting_inputs;
};

struct mrp_wayland_factory_s {
    size_t size;
    const struct wl_interface *interface;
    mrp_wayland_constructor_t constructor;
    mrp_wayland_destructor_t destructor;
};

struct mrp_wayland_interface_s {
    mrp_wayland_t *wl;
    const char *name;
    mrp_wayland_factory_t object_factory;
    mrp_list_hook_t object_list;
};

struct mrp_wayland_object_s {
    MRP_WAYLAND_OBJECT_COMMON;
};


mrp_wayland_t *mrp_wayland_create(const char *display_name,mrp_mainloop_t *ml);
void mrp_wayland_destroy(mrp_wayland_t *wl);

mrp_wayland_t *mrp_wayland_iterate(void **cursor);

bool mrp_wayland_connect(mrp_wayland_t *wl);
void mrp_wayland_flush(mrp_wayland_t *wl);
bool mrp_wayland_disconnect(mrp_wayland_t *wl);

void mrp_wayland_register_window_manager(mrp_wayland_t *wl,
                                         mrp_wayland_window_manager_t *wm);
void mrp_wayland_register_input_manager(mrp_wayland_t *wl,
                                        mrp_wayland_input_manager_t *im);

bool mrp_wayland_register_interface(mrp_wayland_t *wl,
                                    mrp_wayland_factory_t *factory);

void mrp_wayland_register_window_manager_update_callback(mrp_wayland_t *wl,
                        mrp_wayland_window_manager_update_callback_t callback);
void mrp_wayland_register_output_update_callback(mrp_wayland_t *wl,
                        mrp_wayland_output_update_callback_t callback);
void mrp_wayland_register_window_update_callback(mrp_wayland_t *wl,
                        mrp_wayland_window_update_callback_t callback);
void mrp_wayland_register_window_hint_callback(mrp_wayland_t *wl,
                        mrp_wayland_window_hint_callback_t callback);
void mrp_wayland_register_layer_update_callback(mrp_wayland_t *wl,
                        mrp_wayland_layer_update_callback_t callback);
void mrp_wayland_register_area_update_callback(mrp_wayland_t *wl,
                        mrp_wayland_area_update_callback_t callback);
void mrp_wayland_set_scripting_window_data(mrp_wayland_t *, void *);

void mrp_wayland_register_input_manager_update_callback(mrp_wayland_t *wl,
                        mrp_wayland_input_manager_update_callback_t callback);
void mrp_wayland_register_input_update_callback(mrp_wayland_t *wl,
                        mrp_wayland_input_update_callback_t callback);
void mrp_wayland_register_code_update_callback(mrp_wayland_t *wl,
                        mrp_wayland_code_update_callback_t callback);
void mrp_wayland_set_scripting_input_data(mrp_wayland_t *, void *);

void mrp_wayland_create_scripting_windows(mrp_wayland_t *wl, bool create);
void mrp_wayland_create_scripting_outputs(mrp_wayland_t *wl, bool create);
void mrp_wayland_create_scripting_areas(mrp_wayland_t *wl, bool create);
void mrp_wayland_create_scripting_layers(mrp_wayland_t *wl, bool create);
void mrp_wayland_create_scripting_inputs(mrp_wayland_t *wl, bool create);

#define mrp_wayland_foreach(_wl, _i) \
    for ((_i) = NULL;  (_wl = mrp_wayland_iterate(&(_i)));)

#endif /* __MURPHY_WAYLAND_H__ */
