/*
 * Copyright (c) 2012, 2013, Intel Corporation
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

#include <murphy/common/macros.h>
#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/transport.h>
#include <murphy/common/wsck-transport.h>
#include <murphy/common/json.h>
#include <murphy/core/plugin.h>

#include "system-controller.h"

#define DEFAULT_ADDRESS "wsck:127.0.0.1:18081/ico_syc_protocol"

/*
 * plugin argument ids/indices
 */

enum {
    ARG_ADDRESS,                         /* transport address to use */
};


/*
 * system-controller context
 */

typedef struct {
    mrp_context_t   *ctx;                /* murphy context */
    mrp_transport_t *lt;                 /* transport we listen on */
    const char      *addr;               /* address we listen on */
    mrp_list_hook_t  clients;            /* connected clients */
    int              id;                 /* next client id */
} sysctl_t;


/*
 * a system-controller client
 */

typedef struct {
    int              id;                 /* client id */
    sysctl_t        *sc;                 /* system controller context */
    mrp_transport_t *t;                  /* client transport */
    mrp_list_hook_t  hook;               /* to list of clients */
} client_t;


static client_t *create_client(sysctl_t *sc, mrp_transport_t *lt)
{
    client_t *c;

    c = mrp_allocz(sizeof(*c));

    if (c != NULL) {
        mrp_list_init(&c->hook);

        c->sc = sc;
        c->t  = mrp_transport_accept(lt, c, MRP_TRANSPORT_REUSEADDR);

        if (c->t != NULL) {
            mrp_list_append(&sc->clients, &c->hook);
            c->id = sc->id++;

            return c;
        }

        mrp_free(c);
    }

    return NULL;
}


static void destroy_client(client_t *c)
{
    if (c != NULL) {
        mrp_list_delete(&c->hook);

        mrp_transport_disconnect(c->t);
        mrp_transport_destroy(c->t);

        mrp_free(c);
    }
}


static void connection_evt(mrp_transport_t *lt, void *user_data)
{
    sysctl_t *sc = (sysctl_t *)user_data;
    client_t *c;

    c = create_client(sc, lt);

    if (c != NULL)
        mrp_log_info("Accepted system controller client connection.");
    else
        mrp_log_error("Failed to accept system controller client connection.");
}


static void closed_evt(mrp_transport_t *t, int error, void *user_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(t);

    if (error != 0)
        mrp_log_error("System controller connection closed with error %d (%s).",
                      error, strerror(error));
    else
        mrp_log_info("System controller client #%d closed connection.", c->id);

    destroy_client(c);
}


static int send_message(client_t *c, mrp_json_t *msg)
{
    const char *s;

    s = mrp_json_object_to_string(msg);

    mrp_debug("sending system controller message to client #%d:", c->id);
    mrp_debug("  %s", s);

    return mrp_transport_sendcustom(c->t, msg);
}


static void recv_evt(mrp_transport_t *t, void *data, void *user_data)
{
    client_t   *c   = (client_t *)user_data;
    mrp_json_t *req = (mrp_json_t *)data;
    const char *s;

    MRP_UNUSED(t);

    s = mrp_json_object_to_string(req);

    mrp_debug("system controller received message from client #%d:", c->id);
    mrp_debug("  %s", s);
}


static int transport_create(sysctl_t *sc)
{
    static mrp_transport_evt_t evt = {
        { .recvcustom     = recv_evt },
        { .recvcustomfrom = NULL     },
        .connection       = connection_evt,
        .closed           = closed_evt,
    };

    mrp_mainloop_t *ml = sc->ctx->ml;
    mrp_sockaddr_t  addr;
    socklen_t       len;
    const char     *type, *opt, *val;
    int             flags;

    len = mrp_transport_resolve(NULL, sc->addr, &addr, sizeof(addr), &type);

    if (len > 0) {
        flags    = MRP_TRANSPORT_REUSEADDR | MRP_TRANSPORT_MODE_CUSTOM;
        sc->lt = mrp_transport_create(ml, type, &evt, sc, flags);

        if (sc->lt != NULL) {
            if (mrp_transport_bind(sc->lt, &addr, len) &&
                mrp_transport_listen(sc->lt, 0)) {
                mrp_log_info("Listening on transport '%s'...", sc->addr);

                opt = MRP_WSCK_OPT_SENDMODE;
                val = MRP_WSCK_SENDMODE_TEXT;
                mrp_transport_setopt(sc->lt, opt, val);

                return TRUE;
            }

            mrp_transport_destroy(sc->lt);
            sc->lt = NULL;
        }
    }
    else
        mrp_log_error("Failed to resolve transport address '%s'.", sc->addr);

    return FALSE;
}


static void transport_destroy(sysctl_t *sc)
{
    mrp_transport_destroy(sc->lt);
    sc->lt = NULL;
}


static int plugin_init(mrp_plugin_t *plugin)
{
    sysctl_t *sc;

    sc = mrp_allocz(sizeof(*sc));

    if (sc != NULL) {
        mrp_list_init(&sc->clients);

        sc->id      = 1;
        sc->ctx     = plugin->ctx;
        sc->addr    = plugin->args[ARG_ADDRESS].str;

        if (!transport_create(sc))
            goto fail;

        return TRUE;
    }


 fail:
    if (sc != NULL) {
        transport_destroy(sc);

        mrp_free(sc);
    }

    return FALSE;
}


static void plugin_exit(mrp_plugin_t *plugin)
{
    sysctl_t *sc = (sysctl_t *)plugin->data;

    transport_destroy(sc);

    mrp_free(sc);
}



#define PLUGIN_DESCRIPTION "Murphy system controller plugin."
#define PLUGIN_HELP        "Murphy system controller plugin."
#define PLUGIN_AUTHORS     "Krisztian Litkey <kli@iki.fi>"
#define PLUGIN_VERSION     MRP_VERSION_INT(0, 0, 1)

static mrp_plugin_arg_t plugin_args[] = {
    MRP_PLUGIN_ARGIDX(ARG_ADDRESS, STRING, "address", DEFAULT_ADDRESS)
};

MURPHY_REGISTER_PLUGIN("system-controller",
                       PLUGIN_VERSION, PLUGIN_DESCRIPTION, PLUGIN_AUTHORS,
                       PLUGIN_HELP, MRP_SINGLETON, plugin_init, plugin_exit,
                       plugin_args, MRP_ARRAY_SIZE(plugin_args),
                       NULL, 0,
                       NULL, 0,
                       NULL);
