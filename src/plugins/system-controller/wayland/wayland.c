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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <murphy/common.h>

#include "wayland.h"
#include "output.h"
#include "layer.h"


static uint32_t oid_hash(const void *);
static uint32_t wid_hash(const void *);
static uint32_t lid_hash(const void *);
static int id_compare(const void *, const void *);

static const char *get_display_name(mrp_wayland_t *w);
static void display_io_watch(mrp_io_watch_t *, int, mrp_io_event_t, void *);
static void object_create(mrp_wayland_interface_t *, uint32_t, uint32_t);
static void global_available_callback(void *, struct wl_registry *, uint32_t,
                                      const char *, uint32_t);
static void global_remove_callback(void *, struct wl_registry *, uint32_t);

static mrp_wayland_t **instances;
static size_t ninstance;


mrp_wayland_t *mrp_wayland_create(const char *display_name, mrp_mainloop_t *ml)
{
    mrp_wayland_t *wl;
    mrp_htbl_config_t icfg, ocfg, wcfg, lcfg, acfg;

    MRP_ASSERT(ml, "invalid argument");

    if (!(wl = mrp_allocz(sizeof(mrp_wayland_t))))
        mrp_log_error("can't allocate memory for wayland");
    else {
        memset(&icfg, 0, sizeof(icfg));
        icfg.nentry = MRP_WAYLAND_INTERFACE_MAX;
        icfg.comp = mrp_string_comp;
        icfg.hash = mrp_string_hash;
        icfg.nbucket = MRP_WAYLAND_INTERFACE_BUCKETS;

        memset(&ocfg, 0, sizeof(ocfg));
        ocfg.nentry = MRP_WAYLAND_OUTPUT_MAX;
        ocfg.comp = id_compare;
        ocfg.hash = oid_hash;
        ocfg.nbucket = MRP_WAYLAND_OUTPUT_BUCKETS;

        memset(&wcfg, 0, sizeof(wcfg));
        wcfg.nentry = MRP_WAYLAND_WINDOW_MAX;
        wcfg.comp = id_compare;
        wcfg.hash = wid_hash;
        wcfg.nbucket = MRP_WAYLAND_WINDOW_BUCKETS;

        memset(&lcfg, 0, sizeof(lcfg));
        lcfg.nentry = MRP_WAYLAND_LAYER_MAX + MRP_WAYLAND_LAYER_BUILTIN;
        lcfg.comp = id_compare;
        lcfg.hash = lid_hash;
        lcfg.nbucket = MRP_WAYLAND_LAYER_BUCKETS;

        memset(&acfg, 0, sizeof(acfg));
        acfg.nentry = MRP_WAYLAND_AREA_MAX;
        acfg.comp = mrp_string_comp;
        acfg.hash = mrp_string_hash;
        acfg.nbucket = MRP_WAYLAND_AREA_BUCKETS;

        wl->display_name = display_name ? mrp_strdup(display_name) : NULL;
        wl->ml = ml;

        wl->registry_listener.global = global_available_callback;
        wl->registry_listener.global_remove = global_remove_callback;

        wl->interfaces = mrp_htbl_create(&icfg);
        wl->outputs = mrp_htbl_create(&ocfg);
        wl->windows = mrp_htbl_create(&wcfg);
        wl->layers = mrp_htbl_create(&lcfg);
        wl->areas = mrp_htbl_create(&acfg);

        instances = mrp_reallocz(instances, ninstance, ninstance + 2);
        instances[ninstance++] = wl;
    }

    return wl;
}

void mrp_wayland_destroy(mrp_wayland_t *wl)
{
    size_t i;

    if (wl) {
        for (i = 0;  i < ninstance;  i++) {
            if (instances[i] == wl) {
                if (i < ninstance-1) {
                    memmove(instances + i, instances + i + 1,
                            (ninstance - (i + 1)) * sizeof(*instances));
                }
                instances[--ninstance] = NULL;
                break;
            }
        }
    }
}

mrp_wayland_t *mrp_wayland_iterate(void **cursor)
{
    ptrdiff_t i;

    if (cursor) {
        i = *cursor - NULL;

        if (i >= 0 && i < (ptrdiff_t)ninstance) {
            *cursor = NULL + (i + 1);
            return instances[i];
        }
    }

    return NULL;
}

bool mrp_wayland_connect(mrp_wayland_t *wl)
{
#define IO_EVENTS MRP_IO_EVENT_IN | MRP_IO_EVENT_ERR | MRP_IO_EVENT_HUP

    struct wl_display *display;
    struct wl_registry *registry;
    mrp_io_watch_t *iow;
    int fd;

    MRP_ASSERT(wl, "invalid argument");
    MRP_ASSERT(wl->ml, "no mainloop");

    if (wl->iow)
        return true; /* we are already connected */

    if (!(display = wl_display_connect(wl->display_name))) {
        mrp_debug("attempt to connect to display '%s' failed",
                  get_display_name(wl));
        return false;
    }

    fd = wl_display_get_fd(display);

    MRP_ASSERT(fd >= 0, "fd for wayland display < 0");

    if (!(registry = wl_display_get_registry(display))) {
        mrp_log_error("can't get registry for display '%s'",
                      get_display_name(wl));
        wl_display_disconnect(display);
        return false;
    }

     if (wl_registry_add_listener(registry, &wl->registry_listener, wl) < 0) {
        mrp_log_error("can't add listener for registry (display '%s')",
                      get_display_name(wl));
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        return false;
    }

     if (!(iow = mrp_add_io_watch(wl->ml,fd,IO_EVENTS,display_io_watch,wl))) {
        mrp_log_error("can't add io watch for display '%s')",
                      get_display_name(wl));
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        return false;
    }

    wl->iow = iow;
    wl->display = display;
    wl->registry = registry;

    mrp_log_info("connecting to wayland display '%s'", get_display_name(wl));

    wl_display_roundtrip(display);

    mrp_debug("queried interfaces");

    wl_display_roundtrip(display);

    mrp_log_info("display '%s' is up and running", get_display_name(wl));

    return true;

#undef IO_EVENTS
}

void mrp_wayland_flush(mrp_wayland_t *wl)
{
    MRP_ASSERT(wl, "invalid argument");

    if (wl->display) {
        mrp_debug("calling wl_display_flush()");
        wl_display_flush(wl->display);
    }
}


static int update_layers(void *key, void *object, void *ud)
{
    mrp_wayland_window_manager_t *wm = (mrp_wayland_window_manager_t *)ud;
    mrp_wayland_layer_t *layer = (mrp_wayland_layer_t *)object;

    MRP_UNUSED(key);

    mrp_debug("register window manager to layer %u/'%s'",
              layer->layerid, layer->name);

    layer->wm = wm;

    return MRP_HTBL_ITER_MORE;
}


void mrp_wayland_register_window_manager(mrp_wayland_t *wl,
                                         mrp_wayland_window_manager_t *wm)
{
    wl->wm = wm;

    mrp_htbl_foreach(wl->layers, update_layers, wm);
}


bool mrp_wayland_register_interface(mrp_wayland_t *wl,
                                    mrp_wayland_factory_t *factory)
{
    mrp_wayland_interface_t *wif;
    const char *name;

    MRP_ASSERT(wl && factory, "invalid argument");
    MRP_ASSERT(factory->size >= sizeof(mrp_wayland_object_t),
               "invalid object size in factory");
    MRP_ASSERT(factory->interface, "missing factory interface");

    name = factory->interface->name;

    MRP_ASSERT(name, "broken factory interface");

    if (!(wif = mrp_allocz(sizeof(mrp_wayland_interface_t)))) {
        mrp_log_error("can't allocate memory for wayland interface '%s'",
                      name);
        return false;
    }

    wif->wl = wl;
    wif->name = mrp_strdup(name);
    wif->object_factory = *factory;

    mrp_list_init(&wif->object_list);

    if (!mrp_htbl_insert(wl->interfaces, (void *)wif->name, wif)) {
        mrp_log_error("failed to add interface '%s' to hashtable. "
                      "Perhaps already registered ...", wif->name);
        mrp_free((void *)wif->name);
        mrp_free(wif);
        return false;
    }

    mrp_log_info("registered wayland interface '%s'", wif->name);

    return true;
}

void mrp_wayland_register_output_update_callback(mrp_wayland_t *wl,
                               mrp_wayland_output_update_callback_t callback)
{
    MRP_ASSERT(wl, "invalid aruments");

    mrp_debug("registering output_update_callback");

    wl->output_update_callback = callback;
}

void mrp_wayland_register_window_update_callback(mrp_wayland_t *wl,
                               mrp_wayland_window_update_callback_t callback)
{
    MRP_ASSERT(wl, "invalid aruments");

    mrp_debug("registering window_update_callback");

    wl->window_update_callback = callback;
}

void mrp_wayland_register_layer_update_callback(mrp_wayland_t *wl,
                               mrp_wayland_layer_update_callback_t callback)
{
    MRP_ASSERT(wl, "invalid aruments");

    mrp_debug("registering layer_update_callback");

    wl->layer_update_callback = callback;
}

void mrp_wayland_register_area_update_callback(mrp_wayland_t *wl,
                               mrp_wayland_area_update_callback_t callback)
{
    MRP_ASSERT(wl, "invalid aruments");

    mrp_debug("registering area_update_callback");

    wl->area_update_callback = callback;
}

void mrp_wayland_set_scripting_data(mrp_wayland_t *wl, void *data)
{
    MRP_ASSERT(wl, "invalid argument");

    mrp_debug("%sset scripting data", data ? "" : "re");

    wl->scripting_data = data;
}

void mrp_wayland_create_scripting_windows(mrp_wayland_t *wl, bool create)
{
    MRP_ASSERT(wl, "invalid argument");

    mrp_debug("%screate scripting windows", create ? "" : "do not ");

    wl->create_scripting_windows = create;
}


void mrp_wayland_create_scripting_outputs(mrp_wayland_t *wl, bool create)
{
    MRP_ASSERT(wl, "invalid argument");

    mrp_debug("%screate scripting outputs", create ? "" : "do not ");

    wl->create_scripting_outputs = create;
}


void mrp_wayland_create_scripting_areas(mrp_wayland_t *wl, bool create)
{
    MRP_ASSERT(wl, "invalid argument");

    mrp_debug("%screate scripting areas", create ? "" : "do not ");

    wl->create_scripting_areas = create;
}


void mrp_wayland_create_scripting_layers(mrp_wayland_t *wl, bool create)
{
    MRP_ASSERT(wl, "invalid argument");

    mrp_debug("%screate scripting layers", create ? "" : "do not ");

    wl->create_scripting_layers = create;
}


static uint32_t oid_hash(const void *pkey)
{
    uint32_t key = *(uint32_t *)pkey;

    return key % MRP_WAYLAND_OUTPUT_BUCKETS;
}

static uint32_t wid_hash(const void *pkey)
{
    uint32_t key = *(uint32_t *)pkey;

    return key % MRP_WAYLAND_WINDOW_BUCKETS;
}

static uint32_t lid_hash(const void *pkey)
{
    uint32_t key = *(uint32_t *)pkey;

    return key % MRP_WAYLAND_LAYER_BUCKETS;
}

static int id_compare(const void *pkey1, const void *pkey2)
{
    int32_t key1 = *(int32_t *)pkey1;
    int32_t key2 = *(int32_t *)pkey2;

    return (key1 == key2) ? 0 : ((key1 < key2) ? -1 : 1);
}

static const char *get_display_name(mrp_wayland_t *wl)
{
    const char *display_name;

    MRP_ASSERT(wl, "invalid argument");

    if (wl->display_name)
        return wl->display_name;

    if ((display_name = getenv("WAYLAND_DISPLAY")) != NULL)
        return display_name;

    return "wayland-0";
}

static void display_io_watch(mrp_io_watch_t *iow,
                             int fd,
                             mrp_io_event_t events,
                             void *ud)
{
    mrp_wayland_t *wl = (mrp_wayland_t *)ud;
    char evnam[32];
    char evlist[1024];
    char *p, *e;

    MRP_UNUSED(fd);

    MRP_ASSERT(wl, "invalid user data");
    MRP_ASSERT(iow == wl->iow, "mismatching io watch");

    if ((events & MRP_IO_EVENT_HUP)) {
        mrp_log_info("display '%s' is gone", get_display_name(wl));

        wl_registry_destroy(wl->registry);
        wl_display_disconnect(wl->display);

        return;
    }

    if ((events & MRP_IO_EVENT_ERR)) {
        mrp_log_error("I/O error on display '%s'", get_display_name(wl));
        return;
    }

    if ((events & MRP_IO_EVENT_IN)) {
        events &= ~MRP_IO_EVENT_IN;

        mrp_debug("dispatching inputs from display '%s'",get_display_name(wl));

        if (wl_display_dispatch(wl->display) < 0) {
            mrp_log_error("failed to dispatch events of display '%s'",
                          get_display_name(wl));
        }

        wl_display_flush(wl->display);
    }

    if (events) {
#       define PRINT(w)                                                    \
            if (p < e) {                                                   \
                p += snprintf(p, e-p, "%s%s", p > evlist ? " " : "", w);   \
            }
#       define CHECK_EVENT(e)                                              \
            if ((events & MRP_IO_EVENT_ ## e)) {                           \
                char *evnam = #e;                                          \
                PRINT(evnam);                                              \
                events &= ~MRP_IO_EVENT_ ## e;                             \
            }

        e = (p = evlist) + (sizeof(evlist) - 1);

        CHECK_EVENT(PRI);
        CHECK_EVENT(OUT);

        if (events) {
            snprintf(evnam, sizeof(evnam), "<unknown 0x%x>", events);
            PRINT(evnam);
        }

        mrp_debug("unhandled io events: %s", evlist);

#       undef CHECK_EVENT
#       undef PRINT
    }
}

static void object_create(mrp_wayland_interface_t *wif,
                          uint32_t name,
                          uint32_t version)
{
    mrp_wayland_t *wl;
    mrp_wayland_factory_t *factory;
    mrp_wayland_object_t *obj;
    struct wl_proxy *proxy;

    MRP_ASSERT(wif, "invalid argument");

    wl = wif->wl;
    factory = &wif->object_factory;

    if (!(obj = mrp_allocz(factory->size))) {
        mrp_log_error("can't allocate %zd byte memory for %u/'%s' object",
                      factory->size, (unsigned int)name, wif->name);
        return;
    }

    proxy = wl_registry_bind(wl->registry, name, factory->interface, 1);

    if (!proxy) {
        mrp_log_error("failed to create proxy for object %u/'%s' on "
                      "display '%s'", name, wif->name, get_display_name(wl));
        mrp_free(obj);
        return;
    }

    mrp_list_init(&obj->interface_link);
    obj->interface = wif;
    obj->name = name;
    obj->version = version;
    obj->proxy = proxy;

    if (factory->constructor) {
        if (!factory->constructor(wl, obj)) {
            mrp_log_error("failed to construct object %u/'%s' on "
                          "display '%s'",name,wif->name, get_display_name(wl));
            wl_proxy_destroy(proxy);
            mrp_free(obj);
        }
    }

    /* TODO: register the object by name */

    mrp_debug("object %u/'%s' on display '%s' created", name, wif->name,
              get_display_name(wl));
}


static void global_available_callback(void *data,
                                      struct wl_registry *registry,
                                      uint32_t name,
                                      const char *interface,
                                      uint32_t version)
{
    mrp_wayland_t *wl = (mrp_wayland_t *)data;
    mrp_wayland_interface_t *wif;

    MRP_ASSERT(wl && registry && interface, "invalid argument");
    MRP_ASSERT(registry == wl->registry, "confused with data structures");

    wif = mrp_htbl_lookup(wl->interfaces, (void *)interface);

    mrp_debug("object %u/%s is up%s", name, interface,
              wif ? "" : " (interface unknown)");

    if (wif) {
        MRP_ASSERT(wif->wl == wl, "confused with data structures");
        object_create(wif, name, version);
    }
}

static void global_remove_callback(void *data,
                                   struct wl_registry *wl_registry,
                                   uint32_t name)
{
    MRP_UNUSED(data);
    MRP_UNUSED(wl_registry);

    mrp_debug("object %u is down", name);
}
