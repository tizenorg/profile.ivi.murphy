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
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include <signal.h>

#include <audio-session-manager.h>

#include <murphy/common.h>

#include "asm-bridge.h"


typedef struct ctx_s {
    mrp_mainloop_t *ml;
    mrp_transport_t *mt;
    int snd_msgq;
    mrp_htbl_t *watched_files;
} ctx_t;

struct watched_file {
    char *watched_file;
    mrp_io_watch_t *wd;
    int32_t instance_id;
    uint32_t handle;
    ctx_t *ctx;
};

static void *wait_queue (void *arg) {
    ASM_msg_lib_to_asm_t msg;

    int *arg_thread = arg;

    int asm_rcv_msgid = arg_thread[0];
    int fd = arg_thread[1];

    if (asm_rcv_msgid == -1) {
        mrp_log_error("failed to create the receive message queue\n");
        exit(1);
    }

    while (1) {
        int ret = msgrcv(asm_rcv_msgid, &msg, sizeof(msg.data), 0, 0);

        if (ret < 0) {
            /* FIXME: proper error handling */
            mrp_log_error("error receiving a message: '%s'!", strerror(errno));
            if (errno == E2BIG) {

                /* remove message from queue */
                msgrcv(asm_rcv_msgid, &msg, sizeof(msg.data), 0, MSG_NOERROR);
            }
            continue;
        }

        /* alignment is fine, since the first argument to the struct is a long */
        write(fd, &msg, sizeof(ASM_msg_lib_to_asm_t));
    }

    return NULL;
}


static void dump_msg(ASM_msg_lib_to_asm_t *msg, ctx_t *ctx)
{
    MRP_UNUSED(ctx);

    mrp_log_info("Message id %ld:", msg->instance_id);

    mrp_log_info("Data handle: %d", msg->data.handle);
    mrp_log_info("     request id:      0x%04x", msg->data.request_id);
    mrp_log_info("     sound event:     0x%04x", msg->data.sound_event);
    mrp_log_info("     sound state:     0x%04x", msg->data.sound_state);
    mrp_log_info("     system resource: 0x%04x", msg->data.system_resource);
#ifdef USE_SECURITY
    {
        int i;

        mrp_log_info("     cookie: ");
        for (i = 0; i < COOKIE_SIZE; i++) {
            mrp_log_info("0x%02x ", msg->data.cookie[i]);
        }
        mrp_log_info("\n");
    }
#endif
}


static int process_msg(ASM_msg_lib_to_asm_t *msg, ctx_t *ctx)
{
    lib_to_asm_t res;
    uint8_t cookie_arr[] = {};
    uint8_t *cookie = cookie_arr;
    int cookie_len = 0;

    dump_msg(msg, ctx);

    /* FIXME: instance_id is signed? */
    res.instance_id = msg->instance_id;
    res.handle = msg->data.handle;
    res.request_id = msg->data.request_id;
    res.sound_event = msg->data.sound_event;
    res.sound_state = msg->data.sound_state;
    res.system_resource = msg->data.system_resource;
#ifdef USE_SECURITY
    {
        cookie_len = COOKIE_SIZE;
        cookie = msg->data.cookie;
    }
#endif

    res.n_cookie_bytes = cookie_len;
    res.cookie = cookie;

    if (!mrp_transport_senddata(ctx->mt, &res, lib_to_asm_descr.tag)) {
        mrp_log_error("Failed to send message to murphy");
        return -1;
    }

    return 0;
}


static void pipe_cb(mrp_mainloop_t *ml, mrp_io_watch_t *w, int fd,
            mrp_io_event_t events, void *user_data)
{
    ASM_msg_lib_to_asm_t msg;
    ctx_t *ctx = user_data;
    int bytes;
    int ret;

    MRP_UNUSED(ml);
    MRP_UNUSED(w);
    MRP_UNUSED(events);

    bytes = read(fd, &msg, sizeof(ASM_msg_lib_to_asm_t));

    if (bytes != sizeof(ASM_msg_lib_to_asm_t)) {
        mrp_log_error("failed to read from the pipe");
        return;
    }

    ret = process_msg(&msg, ctx);

    if (ret < 0) {
        mrp_log_error("error parsing or proxying message");
    }
}


static void read_watch_cb(mrp_mainloop_t *ml, mrp_io_watch_t *w, int fd,
                                  mrp_io_event_t events, void *user_data)
{
    struct watched_file *wf = user_data;
    ctx_t *ctx = wf->ctx;

    MRP_UNUSED(ml);
    MRP_UNUSED(w);

    if (events & MRP_IO_EVENT_IN) {
        uint32_t buf;
        int ret;

        ret = read(fd, &buf, sizeof(uint32_t));

        if (ret == sizeof(uint32_t)) {
            lib_to_asm_cb_t msg;

            msg.instance_id = wf->instance_id;
            msg.handle = wf->handle;
            msg.cb_result = buf;

            if (!mrp_transport_senddata(ctx->mt, &msg, lib_to_asm_cb_descr.tag)) {
                mrp_log_error("Failed to send message to murphy");
            }
        }
    }
    if (events & MRP_IO_EVENT_HUP) {
        /* can we assume that the client went away? */
        mrp_log_error("HUP event from client");
    }

    mrp_htbl_remove(ctx->watched_files, wf->watched_file, TRUE);
    close(fd);
}


static int send_callback_to_client(asm_to_lib_cb_t *msg, ctx_t *ctx)
{
#define ASM_FILENAME_SIZE 64
    char wr_filename[ASM_FILENAME_SIZE];
    char rd_filename[ASM_FILENAME_SIZE];

    int wr_fd = -1;
    int rd_fd = -1;
    uint32_t data;
    int ret;

    struct watched_file *wf = NULL;

    snprintf(wr_filename, ASM_FILENAME_SIZE, "/tmp/ASM.%d.%u",
            msg->instance_id, msg->handle);

    mrp_log_info("writing client preemption to file %s", wr_filename);

    wr_fd = open(wr_filename, O_NONBLOCK | O_WRONLY);
    if (wr_fd < 0) {
        mrp_log_error("failed to open file '%s' for writing: '%s'", wr_filename,
                strerror(errno));
        goto error;
    }

    if (msg->callback_expected) {

        snprintf(rd_filename, ASM_FILENAME_SIZE, "/tmp/ASM.%d.%ur",
            msg->instance_id, msg->handle);
        rd_fd = open(wr_filename, O_NONBLOCK | O_RDONLY);
        if (rd_fd < 0) {
            mrp_log_error("failed to open file '%s' for reading: '%s'",
                    rd_filename, strerror(errno));
            goto error;
        }

        wf = mrp_htbl_lookup(ctx->watched_files, rd_filename);

        if (wf) {
            /* already watched, this is a bad thing */

            mrp_log_error("client %d.%u missed a callback notification",
                    msg->instance_id, msg->handle);
        }
        else {

            wf = mrp_allocz(sizeof(struct watched_file));

            wf->watched_file = mrp_strdup(rd_filename);
            wf->ctx = ctx;
            wf->instance_id = msg->instance_id;
            wf->handle = msg->handle;
            wf->wd = mrp_add_io_watch(ctx->ml, rd_fd, MRP_IO_EVENT_IN | MRP_IO_EVENT_HUP,
                read_watch_cb, wf);

            mrp_htbl_insert(ctx->watched_files, wf->watched_file, wf);
        }
    }

    /* encode the data for sending */
    data = 0x0000ffff;
    data &= msg->handle;
    data |= msg->sound_command << 16;
    data |= msg->event_source << 24;

    ret = write(wr_fd, &data, sizeof(uint32_t));

    if (ret < (int) sizeof(uint32_t)) {
        mrp_log_error("failed to write callback data to %d.%u",
                msg->instance_id, msg->handle);
        goto error;
    }

    mrp_log_error("Wrote data 0x%08x successfully to client", data);

    close(wr_fd);

    return 0;

error:
    if (wf && wf->watched_file) {
        mrp_htbl_remove(ctx->watched_files, wf->watched_file, TRUE);
    }

    if (wr_fd >= 0) {
        close(wr_fd);
    }

    if (rd_fd >= 0) {
        close(wr_fd);
    }

    return -1;
#undef ASM_FILENAME_SIZE
}


static void recvfrom_murphy(mrp_transport_t *t, void *data, uint16_t tag,
                     mrp_sockaddr_t *addr, socklen_t addrlen, void *user_data)
{
    ctx_t *ctx = user_data;

    MRP_UNUSED(t);
    MRP_UNUSED(addr);
    MRP_UNUSED(addrlen);

    switch (tag) {
        case TAG_ASM_TO_LIB:
            {
                asm_to_lib_t *res = data;
                ASM_msg_asm_to_lib_t msg;

                msg.instance_id = res->instance_id;
                msg.data.alloc_handle = res->alloc_handle;
                msg.data.cmd_handle = res->cmd_handle;
                msg.data.result_sound_command = res->result_sound_command;
                msg.data.result_sound_state = res->result_sound_state;
#ifdef USE_SECURITY
                msg.data.check_privilege = res->check_privilege;
#endif

                if (msgsnd(ctx->snd_msgq, (void *) &msg,
                            sizeof(msg.data), 0) < 0) {
                    mrp_log_error("failed to send message to client");
                }
                break;
            }

        case TAG_ASM_TO_LIB_CB:
            {
                if (send_callback_to_client(data, ctx) < 0) {
                    mrp_log_error("failed to send callback message to client");
                }
                break;
            }

        default:
            mrp_log_error("Unknown message received!");
            break;
    }
}


static void recv_murphy(mrp_transport_t *t, void *data, uint16_t tag, void *user_data)
{
    recvfrom_murphy(t, data, tag, NULL, 0, user_data);
}


static void closed_evt(mrp_transport_t *t, int error, void *user_data)
{
    ctx_t *ctx = user_data;

    MRP_UNUSED(t);
    MRP_UNUSED(error);

    mrp_log_error("server closed the connection");

    mrp_mainloop_quit(ctx->ml, 0);
}


static int connect_to_murphy(char *address, ctx_t *ctx)
{
    const char *atype;
    ssize_t alen;
    mrp_sockaddr_t addr;

    static mrp_transport_evt_t evt = {
        { .recvdata = recv_murphy },
        { .recvdatafrom = recvfrom_murphy },
        .closed = closed_evt,
        .connection = NULL
    };

    if (!mrp_msg_register_type(&lib_to_asm_descr) ||
            !mrp_msg_register_type(&asm_to_lib_descr) ||
            !mrp_msg_register_type(&asm_to_lib_cb_descr) ||
            !mrp_msg_register_type(&lib_to_asm_cb_descr)) {
        mrp_log_error("Failed to register message types");
        goto error;
    }

    alen = mrp_transport_resolve(NULL, address, &addr, sizeof(addr), &atype);

    if (alen <= 0) {
        mrp_log_error("Error resolving transport address");
        goto error;
    }

    ctx->mt = mrp_transport_create(ctx->ml, atype, &evt, ctx,
            MRP_TRANSPORT_MODE_CUSTOM | MRP_TRANSPORT_NONBLOCK);

    if (ctx->mt == NULL) {
        mrp_log_error("Failed to create the transport");
        goto error;
    }

#if 0
    if (!mrp_transport_bind(ctx->mt, &addr, alen)) {
        mrp_log_error("Failed to bind the transport to address '%s'", address);
        goto error;
    }
#endif

    if (!mrp_transport_connect(ctx->mt, &addr, alen)) {
        mrp_log_error("Failed to connect the transport");
        goto error;
    }

    return 0;

error:
    if (ctx->mt)
        mrp_transport_destroy(ctx->mt);

    return -1;
}

static void htbl_free_watches(void *key, void *object)
{
    struct watched_file *wf = object;

    MRP_UNUSED(key);

    mrp_free(wf->watched_file);
    if (wf->wd)
        mrp_del_io_watch(wf->wd);

    mrp_free(wf);
}


int main (int argc, char **argv)
{
    pthread_t thread = 0;
    void *res;
    int pipes[2];
    int thread_arg[2];
    mrp_io_watch_t *iow = NULL;
    mrp_io_event_t events = MRP_IO_EVENT_IN;

    int asm_snd_msgid = msgget((key_t)4102, 0666 | IPC_CREAT);
    int asm_rcv_msgid = msgget((key_t)2014, 0666 | IPC_CREAT);

    mrp_htbl_config_t watches_conf;

    ctx_t ctx;

    /* set up the signal handling */

    if (asm_snd_msgid == -1 || asm_rcv_msgid == -1) {
        mrp_log_error("failed to create the message queues\n");
        goto end;
    }

    ctx.ml = NULL;
    ctx.mt = NULL;
    ctx.snd_msgq = asm_snd_msgid;

    if (argc != 2 || strncmp(argv[1], "unxs", 4) != 0) {
        mrp_log_error("Usage: asm-bridge <socket_name>");
        goto end;
    }

    watches_conf.comp = mrp_string_comp;
    watches_conf.hash = mrp_string_hash;
    watches_conf.free = htbl_free_watches;
    watches_conf.nbucket = 0;
    watches_conf.nentry = 10;

    ctx.watched_files = mrp_htbl_create(&watches_conf);

    ctx.ml = mrp_mainloop_create();

    /* Initialize connection to murphyd */

    if (connect_to_murphy(argv[1], &ctx) < 0) {
        goto end;
    }

    /* create a pipe for communicating with the ASM thread */

    if (pipe(pipes) == -1) {
        goto end;
    }

    /* pass the message queue and the pipe writing end to the thread */

    thread_arg[0] = asm_rcv_msgid;
    thread_arg[1] = pipes[1];

    /* start listening to the read end of the pipe */

    iow = mrp_add_io_watch(ctx.ml, pipes[0], events, pipe_cb, &ctx);

    if (!iow) {
        goto end;
    }

    pthread_create(&thread, NULL, wait_queue, thread_arg);

    /* start processing events */

    mrp_mainloop_run(ctx.ml);

    mrp_log_warning("shutting down asm-bridge");

end:
    if (iow)
        mrp_del_io_watch(iow);

    if (ctx.watched_files) {
        mrp_htbl_destroy(ctx.watched_files, TRUE);
    }

    if (thread) {
        pthread_cancel(thread);
        pthread_join(thread, &res);
    }

    /* free the message queues */

    msgctl(asm_snd_msgid, IPC_RMID, 0);
    msgctl(asm_rcv_msgid, IPC_RMID, 0);

    exit(0);
}
