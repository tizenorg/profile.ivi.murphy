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
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>
#include <wait.h>

#include <audio-session-manager.h>

#include <murphy/common.h>
#include <murphy/core.h>

#include <murphy/resource/client-api.h>

#include "resource-asm/asm-bridge.h"


typedef struct {
    uint32_t pid;
    mrp_htbl_t *sets;
    uint32_t n_sets;
    uint32_t current_handle;

    bool monitor; /* if the client has set up a monitor resource */
} client_t;



typedef struct {
    /* configuration */
    const char *address;
    const char *binary;

    /* asm-bridge management */
    pid_t pid;
    mrp_transport_t *mt; /* listening transport */
    mrp_transport_t *t; /* connected transport */
    bool active;

    /* resource management */
    mrp_htbl_t *clients;
    mrp_resource_client_t *resource_client;
    char *zone;

    char *playback_resource;
    char *recording_resource;

    mrp_htbl_t *requests;
    uint32_t current_request;

    /* murphy integration */
    mrp_sighandler_t *sighandler;
    mrp_context_t *ctx;
} asm_data_t;


typedef enum {
    request_type_acquire,
    request_type_release,
    request_type_server_event
} request_type_t;


typedef struct {
    /* immutable */
    uint32_t pid;
    uint32_t handle;
    mrp_resource_set_t *rset;
    uint32_t request_id;
    asm_data_t *ctx;

    /* mutable */
    request_type_t rtype;

    bool monitor;
    bool earjack;
} resource_set_data_t;



enum {
    ARG_ASM_BRIDGE,
    ARG_ASM_ZONE,
    ARG_ASM_TPORT_ADDRESS,
    ARG_ASM_PLAYBACK_RESOURCE,
    ARG_ASM_RECORDING_RESOURCE,
};

static int tport_setup(const char *address, asm_data_t *ctx);

static void *u_to_p(uint32_t u)
{
#ifdef __SIZEOF_POINTER__
#if __SIZEOF_POINTER__ == 8
    uint64_t o = u;
#else
    uint32_t o = u;
#endif
#else
    uint32_t o = o;
#endif
    return (void *) o;
}

static uint32_t p_to_u(const void *p)
{
#ifdef __SIZEOF_POINTER__
#if __SIZEOF_POINTER__ == 8
    uint32_t o = 0;
    uint64_t big = (uint64_t) p;
    o = big & 0xffffffff;
#else
    uint32_t o = (uint32_t) p;
#endif
#else
    uint32_t o = p;
#endif
    return o;
}


static int int_comp(const void *key1, const void *key2)
{
    return key1 != key2;
}


static uint32_t int_hash(const void *key)
{
    return p_to_u(key);
}


static void dump_incoming_msg(lib_to_asm_t *msg, asm_data_t *ctx)
{
    MRP_UNUSED(ctx);

    mrp_log_info(" --> client id:       %u", msg->instance_id);
    mrp_log_info(" --> data handle:     %d", msg->handle);
    mrp_log_info(" --> request id:      0x%04x", msg->request_id);
    mrp_log_info(" --> sound event:     0x%04x", msg->sound_event);
    mrp_log_info(" --> system resource: 0x%04x", msg->system_resource);
    mrp_log_info(" --> state:           0x%04x", msg->sound_state);
#ifdef USE_SECURITY
    {
        int n_cookie = msg->n_cookie_bytes;
        int i;

        mrp_log_info(" --> cookie: ")
        for (i = 0; i < n_cookie, i++) {
            mrp_log_info("0x%02x ", msg->cookie[i]);
        }
        mrp_log_info("\n");
    }
#endif
}



static void dump_outgoing_msg(asm_to_lib_t *msg, asm_data_t *ctx)
{
    MRP_UNUSED(ctx);

    mrp_log_info(" <-- client id:       %u", msg->instance_id);
    mrp_log_info(" <-- alloc handle:    %d", msg->alloc_handle);
    mrp_log_info(" <-- command handle:  %d", msg->cmd_handle);
    mrp_log_info(" <-- sound command:   0x%04x", msg->result_sound_command);
    mrp_log_info(" <-- state:           0x%04x", msg->result_sound_state);
    mrp_log_info(" <-- check privilege: %s",
                            msg->check_privilege ? "TRUE" : "FALSE");
}

#if 0
static uint32_t encode_pid_handle(uint32_t pid, uint32_t handle) {

    uint32_t max_pid = 4194304; /* 2^22 */
    uint32_t max_handle = 1024; /* 2^10 */

    if (pid > max_pid || handle > max_handle) {
        return 0;
    }

    return pid | (handle << 22);
}

static uint32_t get_pid(uint32_t data) {
    uint32_t pid_mask = 0xffffffff >> 10;

    return data & pid_mask;
}


static uint32_t get_handle(uint32_t data) {

    return data >> 22;
}
#endif


static void htbl_free_set(void *key, void *object)
{
    resource_set_data_t *d = object;

    MRP_UNUSED(key);

    mrp_resource_set_destroy(d->rset);

    mrp_free(d);
}


static client_t *create_client(uint32_t pid) {
    client_t *client = mrp_allocz(sizeof(client_t));
    mrp_htbl_config_t set_conf;

    if (!client)
        return NULL;

    set_conf.comp = int_comp;
    set_conf.hash = int_hash;
    set_conf.free = htbl_free_set;
    set_conf.nbucket = 0;
    set_conf.nentry = 10;

    client->sets = mrp_htbl_create(&set_conf);

    if (!client->sets) {
        mrp_free(client);
        return NULL;
    }

    client->pid = pid;
    client->current_handle = 1;
    client->n_sets = 0;

    return client;
}


static void event_cb(uint32_t request_id, mrp_resource_set_t *set, void *data)
{
    resource_set_data_t *d = data;
    asm_data_t *ctx = d->ctx;

    mrp_log_info("Event CB: id %u, set %p", request_id, set);
    mrp_log_info("Resource set %u.%u", d->pid, d->handle);
    mrp_log_info("Advice 0x%08x, Grant 0x%08x",
            mrp_get_resource_set_advice(d->rset),
            mrp_get_resource_set_grant(d->rset));

    switch(d->rtype) {
        case request_type_acquire:
        {
            asm_to_lib_t reply;
            mrp_log_info("callback for acquire request %u", request_id);

            /* expecting next server events */
            d->rtype = request_type_server_event;

            reply.instance_id = d->pid;
            reply.check_privilege = TRUE;
            reply.alloc_handle = d->handle;
            reply.cmd_handle = d->handle;

            reply.result_sound_command = ASM_COMMAND_NONE;

            /* TODO: check the mask properly */
            if (mrp_get_resource_set_grant(d->rset))
                reply.result_sound_state = ASM_STATE_PLAYING;
            else
                reply.result_sound_state = ASM_STATE_STOP;

            d->rtype = request_type_server_event;

            dump_outgoing_msg(&reply, ctx);
            mrp_transport_senddata(d->ctx->t, &reply, TAG_ASM_TO_LIB);
            break;
        }
        case request_type_release:
        {
            asm_to_lib_t reply;
            mrp_log_info("callback for release request %u", request_id);

            /* expecting next server events */
            d->rtype = request_type_server_event;

            reply.instance_id = d->pid;
            reply.check_privilege = TRUE;
            reply.alloc_handle = d->handle;
            reply.cmd_handle = d->handle;

            reply.result_sound_command = ASM_COMMAND_NONE;
            reply.result_sound_state = ASM_STATE_STOP;

            d->rtype = request_type_server_event;

            dump_outgoing_msg(&reply, ctx);
            mrp_transport_senddata(d->ctx->t, &reply, TAG_ASM_TO_LIB);
            break;
        }
        case request_type_server_event:
        {
            asm_to_lib_cb_t reply;
            mrp_log_info("callback for no request %u", request_id);

            reply.instance_id = d->pid;
            reply.handle = d->handle;
            reply.callback_expected = FALSE;

            /* TODO: get the client and see if there is the monitor
             * resource present. If yes, tell the availability state changes
             * through it. */

            /* TODO: check if the d->rset state has actually changed */

            if (mrp_get_resource_set_grant(d->rset))
                reply.sound_command = ASM_COMMAND_PLAY;
            else
                reply.sound_command = ASM_COMMAND_STOP;

            /* FIXME: the player-player case needs to be solved here? */
            reply.event_source = ASM_EVENT_SOURCE_RESOURCE_CONFLICT;

            mrp_transport_senddata(d->ctx->t, &reply, TAG_ASM_TO_LIB_CB);

            break;
        }
    }
}


static asm_to_lib_t *process_msg(lib_to_asm_t *msg, asm_data_t *ctx)
{
    pid_t pid = msg->instance_id;

    asm_to_lib_t *reply;

    reply = mrp_allocz(sizeof(asm_to_lib_t));

    reply->instance_id = pid;
    reply->check_privilege = TRUE;

    reply->alloc_handle = msg->handle;
    reply->cmd_handle = msg->handle;

    reply->result_sound_command = ASM_COMMAND_NONE;
    reply->result_sound_state = ASM_STATE_IGNORE;

    switch(msg->request_id) {
        case ASM_REQUEST_REGISTER:
        {
            client_t *client;
            bool shared = FALSE;
            char *resource = "invalid";

            mrp_log_info("REQUEST: REGISTER");

            /* see if the process already has a client object */
            client = mrp_htbl_lookup(ctx->clients, u_to_p(pid));

            if (!client) {
                client = create_client(pid);
                mrp_htbl_insert(ctx->clients, u_to_p(pid), client);
            }
#if 0
            else {
                /* From Murphy point of view this is actually an error case,
                 * since the application can only belong to one class. This is
                 * a Murphy limitation and should be fixed later. */

                 mrp_log_error("Application tried to register twice");
                 goto error;
            }
#endif

            switch (msg->sound_event) {
                case ASM_EVENT_NONE:
                    break;
                case ASM_EVENT_SHARE_MMPLAYER:
                    resource = "player";
                    shared = TRUE;
                    break;
                case ASM_EVENT_SHARE_MMCAMCORDER:
                    resource = "camera";
                    shared = TRUE;
                    break;
                case ASM_EVENT_SHARE_MMSOUND:
                    resource = "sound";
                    shared = TRUE;
                    break;
                case ASM_EVENT_SHARE_OPENAL:
                    shared = TRUE;
                    break;
                case ASM_EVENT_SHARE_AVSYSTEM:
                    shared = TRUE;
                    break;
                case ASM_EVENT_EXCLUSIVE_MMPLAYER:
                    resource = "player";
                    shared = FALSE;
                    break;
                case ASM_EVENT_EXCLUSIVE_MMCAMCORDER:
                    resource = "camera";
                    shared = FALSE;
                    break;
                case ASM_EVENT_EXCLUSIVE_MMSOUND:
                    resource = "sound";
                    shared = FALSE;
                    break;
                case ASM_EVENT_EXCLUSIVE_OPENAL:
                    shared = FALSE;
                    break;
                case ASM_EVENT_EXCLUSIVE_AVSYSTEM:
                    shared = FALSE;
                    break;
                case ASM_EVENT_NOTIFY:
                    shared = FALSE;
                    break;
                case ASM_EVENT_CALL:
                    resource = "phone";
                    shared = FALSE;
                    break;
                case ASM_EVENT_SHARE_FMRADIO:
                    shared = TRUE;
                    break;
                case ASM_EVENT_EXCLUSIVE_FMRADIO:
                    shared = FALSE;
                    break;
                case ASM_EVENT_EARJACK_UNPLUG:
                    resource = "earjack";
                    shared = TRUE;
                    break;
                case ASM_EVENT_ALARM:
                    shared = FALSE;
                    break;
                case ASM_EVENT_VIDEOCALL:
                    resource = "phone";
                    shared = FALSE;
                    break;
                case ASM_EVENT_MONITOR:
                    resource = "monitor";
                    shared = TRUE;
                    break;
                case ASM_EVENT_RICH_CALL:
                    resource = "phone";
                    shared = FALSE;
                    break;
                default:
                    break;
            }

            if (strcmp(resource, "invalid") == 0) {
                mrp_log_error("unknown resource type: %d", msg->sound_event);
                goto error;
            }
            else {
                uint32_t handle = client->current_handle++;
                resource_set_data_t *d = mrp_allocz(sizeof(resource_set_data_t));

                d->handle = handle;
                d->ctx = ctx;
                d->pid = pid;
                d->rtype = request_type_server_event;
                d->request_id = 0;

                if (strcmp(resource, "earjack") == 0) {
                    mrp_log_info("earjack status request was received");
                    d->earjack = TRUE;
                }
                else if (strcmp(resource, "monitor") == 0) {
                    mrp_log_info("monitor resource was received");
                    /* TODO: tell the available state changes to this pid
                     * via the monitor resource. */
                    client->monitor = TRUE;
                    d->monitor = TRUE;
                }
                else {
                    /* a normal resource request */

                    d->rset = mrp_resource_set_create(ctx->resource_client, 0,
                            0, event_cb, d);

                    if (!d->rset) {
                        mrp_log_error("Failed to create resource set!");
                        goto error;
                    }

                    if (mrp_resource_set_add_resource(d->rset,
                            ctx->playback_resource, shared, NULL, TRUE) < 0) {
                        mrp_log_error("Failed to add playback resource!");
                        mrp_resource_set_destroy(d->rset);
                        mrp_free(d);
                        goto error;
                    }

                    if (mrp_resource_set_add_resource(d->rset,
                            ctx->recording_resource, shared, NULL, TRUE) < 0) {
                        mrp_log_error("Failed to add recording resource!");
                        mrp_resource_set_destroy(d->rset);
                        mrp_free(d);
                        goto error;
                    }

                    if (mrp_application_class_add_resource_set(resource,
                            ctx->zone, d->rset, 0) < 0) {
                        mrp_log_error("Failed to put the rset in a class!");
                        mrp_resource_set_destroy(d->rset);
                        mrp_free(d);
                        goto error;
                    }
                }

                mrp_htbl_insert(client->sets, u_to_p(handle), d);
                client->n_sets++;

                reply->alloc_handle = handle;
                reply->cmd_handle = reply->alloc_handle;

                reply->result_sound_state = ASM_STATE_WAITING;
                reply->result_sound_command = ASM_COMMAND_NONE;
            }

            break;
        }
        case ASM_REQUEST_UNREGISTER:
            {
                client_t *client = mrp_htbl_lookup(ctx->clients, u_to_p(pid));

                mrp_log_info("REQUEST: UNREGISTER");

                if (client) {
                    resource_set_data_t *d;

                    d = mrp_htbl_lookup(client->sets, u_to_p(msg->handle));
                    if (!d || !d->rset) {
                        mrp_log_error("set '%u.%u' not found", pid, msg->handle);
                        goto error;
                    }

                    if (!d->rset) {
                        /* this is a resource request with no associated
                         * murphy resource, meaning a monitor or earjack. */

                        if (d->monitor)
                            client->monitor = FALSE;

                        /* TODO: what to do with the earjack unregister case? */
                    }

                    /* the resource set id destroyed when it's removed from the
                     * table */
                    mrp_htbl_remove(client->sets, u_to_p(msg->handle), TRUE);
                    client->n_sets--;

                    if (client->n_sets <= 0) {
                        mrp_htbl_remove(ctx->clients, u_to_p(pid), TRUE);
                        client = NULL;
                    }
                }

                /* TODO: free memory and check if the resource set is empty when
                 * the resource library supports it */

                /* no reply needed! */
                goto noreply;

                break;
            }
        case ASM_REQUEST_SETSTATE:
            {
                client_t *client = mrp_htbl_lookup(ctx->clients, u_to_p(pid));

                resource_set_data_t *d;

                mrp_log_info("REQUEST: SET STATE");

                if (!client) {
                    mrp_log_error("client '%u' not found", pid);
                    goto error;
                }

                d = mrp_htbl_lookup(client->sets, u_to_p(msg->handle));
                if (!d || !d->rset) {
                    mrp_log_error("set '%u.%u' not found", pid, msg->handle);
                    goto error;
                }

                d->request_id = ++ctx->current_request;

                switch(msg->sound_state) {
                    case ASM_STATE_PLAYING:
                    {
                        d->rtype = request_type_acquire;

                        mrp_log_info("requesting acquisition of playback rights"
                                " for set '%u.%u' (id: %u)", pid, msg->handle,
                                d->request_id);

                        mrp_resource_set_acquire(d->rset, d->request_id);

                        break;
                    }
                    case ASM_STATE_STOP:
                    case ASM_STATE_PAUSE:
                    {
                        d->rtype = request_type_release;

                        mrp_log_info("requesting release of playback rights for"
                                " set '%u.%u' (id: %u)", pid, msg->handle,
                                d->request_id);

                        mrp_resource_set_release(d->rset, d->request_id);

                        break;
                    }
                    default:
                    {
                        mrp_log_error("Unknown state: %d", msg->sound_state);
                    }
                }

                goto noreply;

                break;
            }
        case ASM_REQUEST_GETSTATE:
            mrp_log_info("REQUEST: GET STATE");
            /* TODO: get the current resource state for msg->sound_event (which
             * is the application class). Put it to reply->result_sound_state
             * field. */
            break;
        case ASM_REQUEST_GETMYSTATE:
            mrp_log_info("REQUEST: GET MY STATE");
            /* TODO: get the current state for the process (resource set). Put
             * it to reply->result_sound_state field. */
            break;
        case ASM_REQUEST_EMERGENT_EXIT:
            {
                client_t *client = mrp_htbl_lookup(ctx->clients, u_to_p(pid));

                mrp_log_info("REQUEST: EMERGENCY EXIT");

                if (!client) {
                    mrp_log_error("client '%u' not found", pid);
                    goto noreply;
                }

                mrp_htbl_remove(ctx->clients, u_to_p(pid), TRUE);

                goto noreply;
                break;
            }
        case ASM_REQUEST_SET_SUBSESSION:
            mrp_log_info("REQUEST: SET SUBSESSION");
            break;
        case ASM_REQUEST_GET_SUBSESSION:
            mrp_log_info("REQUEST: GET SUBSESSION");
            break;
        default:
            mrp_log_info("REQUEST: UNKNOWN REQUEST");
            break;
    }

    return reply;

error:
    /* TODO: need to write some sort of message back? */

noreply:

    mrp_free(reply);
    return NULL;
}


static void process_cb_msg(lib_to_asm_cb_t *msg, asm_data_t *ctx)
{
    const char *str;

    MRP_UNUSED(ctx);

    /* TODO: this function might tell something to the resource library */

    switch (msg->cb_result) {
        case ASM_CB_RES_IGNORE:
            str = "ignore";
            break;
        case ASM_CB_RES_NONE:
            str = "none";
            break;
        case ASM_CB_RES_PAUSE:
            str = "pause";
            break;
        case ASM_CB_RES_PLAYING:
            str = "playing";
            break;
        case ASM_CB_RES_STOP:
            str = "stop";
            break;
        default:
            mrp_log_error("unknown callback state %d", msg->cb_result);
            return;
    }

    mrp_log_info("client %d.%u ended in state '%s' after callback",
            msg->instance_id, msg->handle, str);
}


static void recvdatafrom_evt(mrp_transport_t *t, void *data, uint16_t tag,
                     mrp_sockaddr_t *addr, socklen_t addrlen, void *user_data)
{
    asm_data_t *ctx = user_data;

    MRP_UNUSED(addr);
    MRP_UNUSED(addrlen);

    switch (tag) {
        case TAG_LIB_TO_ASM:
            {
                lib_to_asm_t *msg = data;
                asm_to_lib_t *reply;

                /* client requests something from us */

                dump_incoming_msg(msg, ctx);

                reply = process_msg(msg, ctx);
                if (reply) {
                    dump_outgoing_msg(reply, ctx);
                    mrp_transport_senddata(t, reply, TAG_ASM_TO_LIB);
                }
                break;
            }
        case TAG_LIB_TO_ASM_CB:
            {
                lib_to_asm_cb_t *msg = data;

                /* client tells us which state it entered after preemption */

                process_cb_msg(msg, ctx);
                break;
            }

        default:
            mrp_log_error("Unknown message received!");
            break;
    }

    mrp_data_free(data, tag);
}


static void recvdata_evt(mrp_transport_t *t, void *data, uint16_t tag, void *user_data)
{
    recvdatafrom_evt(t, data, tag, NULL, 0, user_data);
}



static void closed_evt(mrp_transport_t *t, int error, void *user_data)
{
#if 1
    asm_data_t *ctx = user_data;

    MRP_UNUSED(error);

    mrp_log_info("closed!");

    mrp_transport_disconnect(t);
    mrp_transport_destroy(t);

    ctx->t = NULL;

    /* TODO: start the listening socket again and relaunch the binary? */
#endif
}


static void connection_evt(mrp_transport_t *lt, void *user_data)
{
    asm_data_t *ctx = user_data;

    mrp_log_info("connection!");

    if (ctx->t) {
        mrp_log_error("Already connected");
    }
    else {
        ctx->t = mrp_transport_accept(lt, ctx, 0);
    }

    /* close the listening socket, since we only have one client */

    mrp_transport_destroy(lt);
    ctx->mt = NULL;
}



static int tport_setup(const char *address, asm_data_t *ctx)
{
    const char *atype;
    ssize_t alen;
    mrp_sockaddr_t addr;

    struct stat statbuf;

    static mrp_transport_evt_t evt = {
        { .recvdata = recvdata_evt },
        { .recvdatafrom = recvdatafrom_evt },
        .closed = closed_evt,
        .connection = connection_evt
    };

    alen = mrp_transport_resolve(NULL, address, &addr, sizeof(addr), &atype);

    if (alen <= 0) {
        mrp_log_error("Error resolving transport address");
        goto error;
    }

    /* remove the old socket if present */

    if (strcmp(atype, "unxs") == 0) {
        char *path = addr.unx.sun_path;
        if (path[0] == '/') {
            /* if local socket and file exists, remove it */
            if (stat(path, &statbuf) == 0) {
                if (S_ISSOCK(statbuf.st_mode)) {
                    if (unlink(path) < 0) {
                        mrp_log_error("error removing the socket");
                        goto error;
                    }
                }
                else {
                    mrp_log_error("a file where the socket should be created");
                    goto error;
                }
            }
        }
    }

    ctx->mt = mrp_transport_create(ctx->ctx->ml, atype, &evt, ctx,
            MRP_TRANSPORT_MODE_CUSTOM | MRP_TRANSPORT_NONBLOCK);

    if (ctx->mt == NULL) {
        mrp_log_error("Failed to create the transport");
        goto error;
    }

    if (!mrp_transport_bind(ctx->mt, &addr, alen)) {
        mrp_log_error("Failed to bind the transport to address '%s'", address);
        goto error;
    }


    if (!mrp_transport_listen(ctx->mt, 5)) {
        mrp_log_error("Failed to listen to transport");
        goto error;
    }

    return 0;

error:
    if (ctx->mt) {
        mrp_transport_destroy(ctx->mt);
        ctx->mt = NULL;
    }

    return -1;
}

static void signal_handler(mrp_mainloop_t *ml, mrp_sighandler_t *h,
                           int signum, void *user_data)
{
    asm_data_t *ctx = user_data;

    MRP_UNUSED(ml);
    MRP_UNUSED(h);

    if (signum == SIGCHLD) {
        /* wait for the child */
        if (ctx->pid > 0) {
            int ret;

            mrp_log_info("Received SIGCHLD, waiting for asm-bridge");

            ret = waitpid(ctx->pid, NULL, WNOHANG);

            if (ret == ctx->pid) {
                mrp_log_warning("asm-bridge process died");
                ctx->pid = 0;
            }
        }
    }
}

static void htbl_free_client(void *key, void *object)
{
    MRP_UNUSED(key);

    client_t *client = object;

    mrp_htbl_destroy(client->sets, TRUE);

    /* TODO: free memory when resource API allows that */
    mrp_free(client);
}

static int asm_init(mrp_plugin_t *plugin)
{
    mrp_plugin_arg_t *args = plugin->args;
    asm_data_t *ctx = mrp_allocz(sizeof(asm_data_t));
    pid_t pid;
    mrp_htbl_config_t client_conf;

    if (!ctx) {
        goto error;
    }

    ctx->ctx = plugin->ctx;
    ctx->address = args[ARG_ASM_TPORT_ADDRESS].str;
    ctx->binary = args[ARG_ASM_BRIDGE].str;
    ctx->zone = args[ARG_ASM_ZONE].str;

    ctx->playback_resource = args[ARG_ASM_PLAYBACK_RESOURCE].str;
    ctx->recording_resource = args[ARG_ASM_RECORDING_RESOURCE].str;

    /* create the transport and put it to listen mode */

    if (!mrp_msg_register_type(&asm_to_lib_descr)) {
        mrp_log_error("Failed to register message type asm_to_lib");
        goto error;
    }

    if (!mrp_msg_register_type(&lib_to_asm_descr)) {
        mrp_log_error("Failed to register message type lib_to_asm");
        goto error;
    }

    if (!mrp_msg_register_type(&asm_to_lib_cb_descr)) {
        mrp_log_error("Failed to register message type asm_to_lib_cb");
        goto error;
    }

    if (!mrp_msg_register_type(&lib_to_asm_cb_descr)) {
        mrp_log_error("Failed to register message type lib_to_asm_cb");
        goto error;
    }

    if (tport_setup(ctx->address, ctx) < 0) {
        goto error;
    }

    /* listen to SIGCHLD signal */

    ctx->sighandler = mrp_add_sighandler(plugin->ctx->ml, SIGCHLD, signal_handler, ctx);

    if (!ctx->sighandler) {
        mrp_log_error("Failed to register signal handling");
        goto error;
    }

    client_conf.comp = int_comp;
    client_conf.hash = int_hash;
    client_conf.free = htbl_free_client;
    client_conf.nbucket = 0;
    client_conf.nentry = 10;

    ctx->clients = mrp_htbl_create(&client_conf);

    if (!ctx->clients) {
        mrp_log_error("Error creating resource set hash table");
        goto error;
    }

    /* create the client structure towards Murphy */

    ctx->resource_client = mrp_resource_client_create("ASM", ctx);

    if (!ctx->resource_client) {
        mrp_log_error("Failed to get a resource client");
        goto error;
    }

    /* fork-exec the asm bridge binary */

    mrp_log_info("going to fork!");

    pid = fork();

    if (pid < 0) {
        mrp_log_error("error launching asm-bridge");
        goto error;
    }
    else if (pid == 0) {
        /* child */
        execl(ctx->binary, ctx->binary, ctx->address, NULL);
        exit(1);
    }
    else {
        /* parent */
        ctx->pid = pid;
        mrp_log_info("child pid is %d", pid);
    }

    plugin->data = ctx;

    return TRUE;

error:

    if (ctx->pid) {
        kill(ctx->pid, SIGTERM);
        ctx->pid = 0;
    }

    if (ctx->sighandler) {
        mrp_del_sighandler(ctx->sighandler);
        ctx->sighandler = NULL;
    }

    if (ctx->resource_client) {
        mrp_resource_client_destroy(ctx->resource_client);
    }

    mrp_free(ctx);

    return FALSE;
}


static void asm_exit(mrp_plugin_t *plugin)
{
    asm_data_t *ctx = plugin->data;

    if (ctx->pid) {
        kill(ctx->pid, SIGTERM);
        ctx->pid = 0;
    }

    if (ctx->mt) {
        mrp_transport_disconnect(ctx->mt);
        mrp_transport_destroy(ctx->mt);
        ctx->mt = NULL;
    }

    if (ctx->t) {
        mrp_transport_disconnect(ctx->t);
        mrp_transport_destroy(ctx->t);
        ctx->t = NULL;
    }

    if (ctx->sighandler) {
        mrp_del_sighandler(ctx->sighandler);
        ctx->sighandler = NULL;
    }

    if (ctx->clients) {
        mrp_htbl_destroy(ctx->clients, TRUE);
        ctx->clients = NULL;
    }

    mrp_resource_client_destroy(ctx->resource_client);

    mrp_free(ctx);
    plugin->data = NULL;
}


#define ASM_DESCRIPTION "A plugin to handle SLP Audio Session Manager client requests."
#define ASM_HELP        "Audio Session Manager backend"
#define ASM_VERSION     MRP_VERSION_INT(0, 0, 1)
#define ASM_AUTHORS     "Ismo Puustinen <ismo.puustinen@intel.com>"

static mrp_plugin_arg_t args[] = {
    MRP_PLUGIN_ARGIDX(ARG_ASM_BRIDGE, STRING, "asm_bridge", "/usr/sbin/asm-bridge"),
    MRP_PLUGIN_ARGIDX(ARG_ASM_ZONE, STRING, "zone", "default"),
    MRP_PLUGIN_ARGIDX(ARG_ASM_TPORT_ADDRESS, STRING, "tport_address", "unxs:/tmp/murphy/asm"),
    MRP_PLUGIN_ARGIDX(ARG_ASM_PLAYBACK_RESOURCE, STRING, "playback_resource", "audio_playback"),
    MRP_PLUGIN_ARGIDX(ARG_ASM_RECORDING_RESOURCE, STRING, "recording_resource", "audio_recording"),
};


MURPHY_REGISTER_PLUGIN("resource-asm",
                       ASM_VERSION, ASM_DESCRIPTION, ASM_AUTHORS, ASM_HELP,
                       MRP_SINGLETON, asm_init, asm_exit,
                       args, MRP_ARRAY_SIZE(args),
                       NULL, 0, NULL, 0, NULL);
