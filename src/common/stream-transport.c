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

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/uio.h>

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>
#include <murphy/common/log.h>
#include <murphy/common/msg.h>
#include <murphy/common/fragbuf.h>
#include <murphy/common/transport.h>

#define TCP4  "tcp4"
#define TCP4L 4
#define TCP6  "tcp6"
#define TCP6L 4
#define UNXS  "unxs"
#define UNXSL 4

#define DEFAULT_SIZE 128                 /* default input buffer size */

typedef struct {
    MRP_TRANSPORT_PUBLIC_FIELDS;         /* common transport fields */
    int             sock;                /* TCP socket */
    mrp_io_watch_t *iow;                 /* socket I/O watch */
    mrp_fragbuf_t  *buf;                 /* fragment buffer */
} strm_t;


static void strm_recv_cb(mrp_io_watch_t *w, int fd, mrp_io_event_t events,
                         void *user_data);
static int strm_disconnect(mrp_transport_t *mt);
static int open_socket(strm_t *t, int family);



static int parse_address(const char *str, int *familyp, char *nodep,
                         size_t nsize, char **servicep, const char **typep)
{
    char       *node, *service;
    const char *type;
    int         family;
    size_t      l, nl;

    node = (char *)str;

    if (!strncmp(node, TCP4":", l=TCP4L+1)) {
        family = AF_INET;
        type   = TCP4;
        node  += l;
    }
    else if (!strncmp(node, TCP6":", l=TCP6L+1)) {
        family = AF_INET6;
        type   = TCP6;
        node  += l;
    }
    else if (!strncmp(node, UNXS":", l=UNXSL+1)) {
        family = AF_UNIX;
        type   = UNXS;
        node  += l;
    }
    else {
        if      (node[0] == '[') family = AF_INET6;
        else if (node[0] == '/') family = AF_UNIX;
        else if (node[0] == '@') family = AF_UNIX;
        else                     family = AF_UNSPEC;

        type = NULL;
    }

    switch (family) {
    case AF_INET:
        service = strrchr(node, ':');
        if (service == NULL) {
            errno = EINVAL;
            return -1;
        }

        nl = service - node;
        service++;

    case AF_INET6:
        service = strrchr(node, ':');

        if (service == NULL || service == node) {
            errno = EINVAL;
            return -1;
        }

        if (node[0] == '[') {
            node++;

            if (service[-1] != ']') {
                errno = EINVAL;
                return -1;
            }

            nl = service - node - 1;
        }
        else
            nl = service - node;

        service++;
        break;

    case AF_UNSPEC:
        if (!strncmp(node, "tcp:", l=4))
            node += l;
        service = strrchr(node, ':');

        if (service == NULL || service == node) {
            errno = EINVAL;
            return -1;
        }

        if (node[0] == '[') {
            node++;
            family = AF_INET6;

            if (service[-1] != ']') {
                errno = EINVAL;
                return -1;
            }

            nl = service - node - 1;
        }
        else {
            family = AF_INET;
            nl = service - node;
        }
        service++;
        break;

    case AF_UNIX:
        service = NULL;
        nl      = strlen(node);
    }

    if (nl > nsize) {
        errno = ENOMEM;
        return -1;
    }

    strncpy(nodep, node, nl);
    nodep[nl] = '\0';
    *servicep = service;
    *familyp  = family;
    if (typep != NULL)
        *typep = type;

    return 0;
}


static socklen_t strm_resolve(const char *str, mrp_sockaddr_t *addr,
                              socklen_t size, const char **typep)
{
    struct addrinfo    *ai, hints;
    struct sockaddr_un *un;
    char                node[512], *port;
    socklen_t           len;

    mrp_clear(&hints);

    if (parse_address(str, &hints.ai_family, node, sizeof(node),
                      &port, typep) < 0)
        return 0;

    switch (hints.ai_family) {
    case AF_UNIX:
        un  = &addr->unx;
        len = MRP_OFFSET(typeof(*un), sun_path) + strlen(node) + 1;

        if (size < len)
            errno = ENOMEM;
        else {
            un->sun_family = AF_UNIX;
            strcpy(un->sun_path, node);
            if (un->sun_path[0] == '@')
                un->sun_path[0] = '\0';
        }
        break;

    case AF_INET:
    case AF_INET6:
    default:
        if (getaddrinfo(node, port, &hints, &ai) == 0) {
            if (ai->ai_addrlen <= size) {
                memcpy(addr, ai->ai_addr, ai->ai_addrlen);
                len = ai->ai_addrlen;
            }
            else
                len = 0;

            freeaddrinfo(ai);
        }
        else
            len = 0;
    }

    return len;
}


static int strm_open(mrp_transport_t *mt)
{
    strm_t *t = (strm_t *)mt;

    t->sock = -1;

    return TRUE;
}


static int strm_createfrom(mrp_transport_t *mt, void *conn)
{
    strm_t           *t = (strm_t *)mt;
    mrp_io_event_t   events;
    int              on;
    long             nb;

    t->sock = *(int *)conn;

    if (t->sock >= 0) {
        if (mt->flags & MRP_TRANSPORT_REUSEADDR) {
            on = 1;
            setsockopt(t->sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        }
        if (mt->flags & MRP_TRANSPORT_NONBLOCK) {
            nb = 1;
            fcntl(t->sock, F_SETFL, O_NONBLOCK, nb);
        }

        if (t->connected) {
            t->buf = mrp_fragbuf_create(TRUE, 0);

            if (t->buf != NULL) {
                events = MRP_IO_EVENT_IN | MRP_IO_EVENT_HUP;
                t->iow = mrp_add_io_watch(t->ml, t->sock, events,
                                          strm_recv_cb, t);

                if (t->iow != NULL)
                    return TRUE;

                mrp_fragbuf_destroy(t->buf);
                t->buf = NULL;
            }
        }
    }

    return FALSE;
}


static int strm_bind(mrp_transport_t *mt, mrp_sockaddr_t *addr,
                     socklen_t addrlen)
{
    strm_t *t = (strm_t *)mt;

    if (t->sock != -1 || open_socket(t, addr->any.sa_family)) {
        if (bind(t->sock, &addr->any, addrlen) == 0)
            return TRUE;
    }

    return FALSE;
}


static int strm_listen(mrp_transport_t *mt, int backlog)
{
    strm_t *t = (strm_t *)mt;

    if (t->sock != -1 && t->iow != NULL && t->evt.connection != NULL) {
        if (listen(t->sock, backlog) == 0) {
            t->listened = TRUE;
            return TRUE;
        }
    }

    return FALSE;
}


static int strm_accept(mrp_transport_t *mt, mrp_transport_t *mlt)
{
    strm_t         *t, *lt;
    mrp_sockaddr_t  addr;
    socklen_t       addrlen;
    mrp_io_event_t  events;
    int             on;
    long            nb;

    t  = (strm_t *)mt;
    lt = (strm_t *)mlt;

    addrlen = sizeof(addr);
    t->sock = accept(lt->sock, &addr.any, &addrlen);
    t->buf  = mrp_fragbuf_create(TRUE, 0);

    if (t->sock >= 0 && t->buf != NULL) {
        if (mt->flags & MRP_TRANSPORT_REUSEADDR) {
            on = 1;
            setsockopt(t->sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        }
        if (mt->flags & MRP_TRANSPORT_NONBLOCK) {
            nb = 1;
            fcntl(t->sock, F_SETFL, O_NONBLOCK, nb);
        }
        if (mt->flags & MRP_TRANSPORT_CLOEXEC) {
            on = 1;
            fcntl(t->sock, F_SETFL, O_CLOEXEC, on);
        }

        events = MRP_IO_EVENT_IN | MRP_IO_EVENT_HUP;
        t->iow = mrp_add_io_watch(t->ml, t->sock, events, strm_recv_cb, t);

        if (t->iow != NULL)
            return TRUE;
        else {
            close(t->sock);
            t->sock = -1;
        }
    }
    else
        mrp_fragbuf_destroy(t->buf);

    return FALSE;
}


static void strm_close(mrp_transport_t *mt)
{
    strm_t *t = (strm_t *)mt;

    mrp_del_io_watch(t->iow);
    t->iow = NULL;

    mrp_fragbuf_destroy(t->buf);
    t->buf = NULL;

    if (t->sock >= 0){
        close(t->sock);
        t->sock = -1;
    }
}


static void strm_recv_cb(mrp_io_watch_t *w, int fd, mrp_io_event_t events,
                         void *user_data)
{
    strm_t          *t  = (strm_t *)user_data;
    mrp_transport_t *mt = (mrp_transport_t *)t;
    void            *data, *buf;
    uint32_t         pending;
    size_t           size;
    ssize_t          n;
    int              error;

    MRP_UNUSED(w);

    if (events & MRP_IO_EVENT_IN) {
        if (MRP_UNLIKELY(mt->listened != 0)) {
            MRP_TRANSPORT_BUSY(mt, {
                    mt->evt.connection(mt, mt->user_data);
                });

            t->check_destroy(mt);
            return;
        }

        while (ioctl(fd, FIONREAD, &pending) == 0 && pending > 0) {
            buf = mrp_fragbuf_alloc(t->buf, pending);

            if (buf == NULL) {
                error = ENOMEM;
            fatal_error:
            closed:
                strm_disconnect(mt);

                if (t->evt.closed != NULL)
                    MRP_TRANSPORT_BUSY(mt, {
                            mt->evt.closed(mt, error, mt->user_data);
                        });

                t->check_destroy(mt);
                return;
            }

            n = read(fd, buf, pending);

            if (n >= 0) {
                if (n < (ssize_t)pending)
                    mrp_fragbuf_trim(t->buf, buf, pending, n);
            }

            if (n < 0 && errno != EAGAIN) {
                error = EIO;
                goto fatal_error;
            }
        }

        data = NULL;
        size = 0;
        while (mrp_fragbuf_pull(t->buf, &data, &size)) {
            error = t->recv_data(mt, data, size, NULL, 0);

            if (error)
                goto fatal_error;

            if (t->check_destroy(mt))
                return;
        }
    }

    if (events & MRP_IO_EVENT_HUP) {
        error = 0;
        goto closed;
    }
}


static int open_socket(strm_t *t, int family)
{
    mrp_io_event_t events;
    int            on;
    long           nb;

    t->sock = socket(family, SOCK_STREAM, 0);

    if (t->sock != -1) {
        if (t->flags & MRP_TRANSPORT_REUSEADDR) {
            on = 1;
            setsockopt(t->sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        }
        if (t->flags & MRP_TRANSPORT_NONBLOCK) {
            nb = 1;
            fcntl(t->sock, F_SETFL, O_NONBLOCK, nb);
        }
        if (t->flags & MRP_TRANSPORT_CLOEXEC) {
            on = 1;
            fcntl(t->sock, F_SETFL, O_CLOEXEC, on);
        }

        events = MRP_IO_EVENT_IN | MRP_IO_EVENT_HUP;
        t->iow = mrp_add_io_watch(t->ml, t->sock, events, strm_recv_cb, t);

        if (t->iow != NULL)
            return TRUE;
        else {
            close(t->sock);
            t->sock = -1;
        }
    }

    return FALSE;
}


static int strm_connect(mrp_transport_t *mt, mrp_sockaddr_t *addr,
                        socklen_t addrlen)
{
    strm_t         *t    = (strm_t *)mt;
    int             on;
    long            nb;
    mrp_io_event_t  events;

    t->sock = socket(addr->any.sa_family, SOCK_STREAM, 0);

    if (t->sock < 0)
        return FALSE;

    if (connect(t->sock, &addr->any, addrlen) == 0) {
        t->buf = mrp_fragbuf_create(TRUE, 0);

        if (t->buf != NULL) {
            events = MRP_IO_EVENT_IN | MRP_IO_EVENT_HUP;
            t->iow = mrp_add_io_watch(t->ml, t->sock, events, strm_recv_cb, t);

            if (t->iow != NULL) {
                on = 1;
                setsockopt(t->sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
                nb = 1;
                fcntl(t->sock, F_SETFL, O_NONBLOCK, nb);

                return TRUE;
            }

            mrp_fragbuf_destroy(t->buf);
            t->buf = NULL;
        }
    }

    if (t->sock != -1) {
        close(t->sock);
        t->sock = -1;
    }

    return FALSE;
}


static int strm_disconnect(mrp_transport_t *mt)
{
    strm_t *t = (strm_t *)mt;

    if (t->connected/* || t->iow != NULL*/) {
        mrp_del_io_watch(t->iow);
        t->iow = NULL;

        shutdown(t->sock, SHUT_RDWR);

        mrp_fragbuf_destroy(t->buf);
        t->buf = NULL;

        return TRUE;
    }
    else
        return FALSE;
}


static int strm_send(mrp_transport_t *mt, mrp_msg_t *msg)
{
    strm_t        *t = (strm_t *)mt;
    struct iovec  iov[2];
    void         *buf;
    ssize_t       size, n;
    uint32_t      len;

    if (t->connected) {
        size = mrp_msg_default_encode(msg, &buf);

        if (size >= 0) {
            len = htobe32(size);
            iov[0].iov_base = &len;
            iov[0].iov_len  = sizeof(len);
            iov[1].iov_base = buf;
            iov[1].iov_len  = size;

            n = writev(t->sock, iov, 2);
            mrp_free(buf);

            if (n == (ssize_t)(size + sizeof(len)))
                return TRUE;
            else {
                if (n == -1 && errno == EAGAIN) {
                    mrp_log_error("%s(): XXX TODO: this sucks, need to add "
                                  "output queuing for strm-transport.",
                                  __FUNCTION__);
                }
            }
        }
    }

    return FALSE;
}


static int strm_sendraw(mrp_transport_t *mt, void *data, size_t size)
{
    strm_t  *t = (strm_t *)mt;
    ssize_t  n;

    if (t->connected) {
        n = write(t->sock, data, size);

        if (n == (ssize_t)size)
            return TRUE;
        else {
            if (n == -1 && errno == EAGAIN) {
                mrp_log_error("%s(): XXX TODO: this sucks, need to add "
                              "output queuing for strm-transport.",
                              __FUNCTION__);
            }
        }
    }

    return FALSE;
}


static int strm_senddata(mrp_transport_t *mt, void *data, uint16_t tag)
{
    strm_t           *t = (strm_t *)mt;
    mrp_data_descr_t *type;
    ssize_t           n;
    void             *buf;
    size_t            size, reserve, len;
    uint32_t         *lenp;
    uint16_t         *tagp;

    if (t->connected) {
        type = mrp_msg_find_type(tag);

        if (type != NULL) {
            reserve = sizeof(*lenp) + sizeof(*tagp);
            size    = mrp_data_encode(&buf, data, type, reserve);

            if (size > 0) {
                lenp  = buf;
                len   = size - sizeof(*lenp);
                tagp  = buf + sizeof(*lenp);
                *lenp = htobe32(len);
                *tagp = htobe16(tag);

                n = write(t->sock, buf, len + sizeof(*lenp));

                mrp_free(buf);

                if (n == (ssize_t)(len + sizeof(*lenp)))
                    return TRUE;
                else {
                    if (n == -1 && errno == EAGAIN) {
                        mrp_log_error("%s(): XXX TODO: this sucks, need to add"
                                      " output queueing for strm-transport.",
                                      __FUNCTION__);
                    }
                }
            }
        }
    }

    return FALSE;
}


MRP_REGISTER_TRANSPORT(tcp4, TCP4, strm_t, strm_resolve,
                       strm_open, strm_createfrom, strm_close, NULL,
                       strm_bind, strm_listen, strm_accept,
                       strm_connect, strm_disconnect,
                       strm_send, NULL,
                       strm_sendraw, NULL,
                       strm_senddata, NULL,
                       NULL, NULL);

MRP_REGISTER_TRANSPORT(tcp6, TCP6, strm_t, strm_resolve,
                       strm_open, strm_createfrom, strm_close, NULL,
                       strm_bind, strm_listen, strm_accept,
                       strm_connect, strm_disconnect,
                       strm_send, NULL,
                       strm_sendraw, NULL,
                       strm_senddata, NULL,
                       NULL, NULL);

MRP_REGISTER_TRANSPORT(unxstrm, UNXS, strm_t, strm_resolve,
                       strm_open, strm_createfrom, strm_close, NULL,
                       strm_bind, strm_listen, strm_accept,
                       strm_connect, strm_disconnect,
                       strm_send, NULL,
                       strm_sendraw, NULL,
                       strm_senddata, NULL,
                       NULL, NULL);
