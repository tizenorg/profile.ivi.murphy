/*
 * Copyright (c) 2014, Intel Corporation
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
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>
#include <linux/filter.h>

#include <murphy/common/macros.h>
#include <murphy/common/log.h>
#include <murphy/common/debug.h>
#include <murphy/common/mm.h>
#include <murphy/common/list.h>
#include <murphy/common/refcnt.h>
#include <murphy/common/file-utils.h>
#include <murphy/common/mainloop.h>

#include <murphy/common/process-watch.h>


static int netlink_open(mrp_mainloop_t *ml);
static void netlink_close(void);
static void netlink_update_filter(void);
static void netlink_dump_event(struct proc_event *e);

static int             nl_sock  = -1;    /* netlink socket */
static mrp_io_watch_t *nl_watch = NULL;  /* I/O watch for netlink socket */
static int             nl_seq   = 0;     /* request sequence number */
static bool            nl_ack   = false; /* whether ACK by kernel */
static int             no_thrd  = 0;     /* ignore threads */
static int             nwatch   = 0;     /* number of (active) watches */
static MRP_LIST_HOOK(watches);           /* list of process watches */


/*
 * a process watch
 */

struct mrp_proc_watch_s {
    mrp_list_hook_t        hook;         /* to list of watches */
    mrp_proc_event_type_t  events;       /* events of interest */
    mrp_proc_filter_t     *filter;       /* extra process filters */
    mrp_proc_watch_cb_t    cb;           /* event callback */
    mrp_mainloop_t        *ml;           /* mainloop */
    void                  *user_data;    /* opaque user data */
    mrp_refcnt_t           refcnt;       /* reference count */
    int                    recursed : 1; /* created by MRP_PROC_RECURSE */
};


/*
 * a synthetic event (generated while scanning /proc)
 */

typedef struct {
    mrp_list_hook_t   hook;              /* to list of all generated events */
    struct proc_event event;             /* synthetic event */
} synthev_t;


/*
 * a block of BPF filter instructions
 */

typedef struct {
    struct sock_filter *insns;           /* instruction block */
    int                 ninsn;           /* size of block */
    struct sock_filter *ip;              /* insertion pointer */
} bpf_t;


static mrp_proc_watch_t *add_watch(mrp_mainloop_t *ml,
                                   mrp_proc_event_type_t mask,
                                   mrp_proc_filter_t *filter,
                                   mrp_proc_watch_cb_t cb, void *user_data,
                                   bool recursive);
static void del_watch(mrp_proc_watch_t *w);
static void free_watch(mrp_proc_watch_t *w);
static void scan_processes(mrp_list_hook_t *list, mrp_proc_event_type_t events);
static void deliver_events(mrp_proc_watch_t *w, mrp_list_hook_t *events);
static void free_events(mrp_list_hook_t *events);
static bool check_filter(struct proc_event *e, mrp_proc_filter_t *f);


mrp_proc_watch_t *mrp_ref_proc_watch(mrp_proc_watch_t *w)
{
    return mrp_ref_obj(w, refcnt);
}


int mrp_unref_proc_watch(mrp_proc_watch_t *w)
{
    return mrp_unref_obj(w, refcnt);
}


mrp_proc_watch_t *mrp_add_proc_watch(mrp_mainloop_t *ml,
                                     mrp_proc_event_type_t mask,
                                     mrp_proc_filter_t *filter,
                                     mrp_proc_watch_cb_t cb, void *user_data)
{
    mrp_proc_watch_t *w;
    mrp_list_hook_t   events;

    if (netlink_open(ml) < 0)
        return NULL;

    w = add_watch(ml, mask, filter, cb, user_data, false);

    if (w != NULL) {
        netlink_update_filter();

        if (w->events & MRP_PROC_SCAN_EXISTING) {
            mrp_list_init(&events);
            scan_processes(&events, w->events);
            deliver_events(w, &events);
            free_events(&events);
        }
    }

    return mrp_ref_obj(w, refcnt);
}


void mrp_del_proc_watch(mrp_proc_watch_t *w)
{
    del_watch(w);

    if (mrp_unref_obj(w, refcnt))
        free_watch(w);
}


void mrp_scan_proc(mrp_proc_event_type_t mask, mrp_proc_filter_t *filter,
                   mrp_proc_watch_cb_t cb, void *user_data)
{
    mrp_list_hook_t   events, *p, *n;
    mrp_proc_event_t  event;
    synthev_t        *e;

    mrp_list_init(&events);
    scan_processes(&events, mask);

    mrp_list_foreach(&events, p, n) {
        e = mrp_list_entry(p, typeof(*e), hook);

        if (check_filter(&e->event, filter)) {
            event.raw  = &e->event;
            event.type = event.raw->what;
            cb(NULL, &event, user_data);
        }
    }

    free_events(&events);
}


static mrp_proc_watch_t *add_watch(mrp_mainloop_t *ml,
                                   mrp_proc_event_type_t mask,
                                   mrp_proc_filter_t *filter,
                                   mrp_proc_watch_cb_t cb, void *user_data,
                                   bool recursive)
{
    mrp_proc_watch_t *w;

    w = mrp_allocz(sizeof(*w));

    if (w == NULL)
        return NULL;

    mrp_list_init(&w->hook);
    mrp_refcnt_init(&w->refcnt);
    w->ml        = ml;
    w->events    = mask;
    w->cb        = cb;
    w->user_data = user_data;

    if (filter != NULL) {
        w->filter = mrp_allocz(sizeof(*w->filter));

        if (w->filter == NULL)
            goto fail;

        w->filter->pid = filter->pid;
        w->filter->uid = filter->uid;
        w->filter->gid = filter->gid;

        if (filter->path != NULL) {
            w->filter->path = mrp_strdup(filter->path);

            if (w->filter->path == NULL)
                goto fail;
        }

        if (filter->comm != NULL) {
            w->filter->comm = mrp_strdup(filter->comm);

            if (w->filter->comm == NULL)
                goto fail;
        }
    }

    mrp_list_prepend(&watches, &w->hook);

    if (w->events & MRP_PROC_IGNORE_THREADS)
        no_thrd++;

    w->recursed = recursive;

    nwatch++;

    return w;

 fail:
    if (w->filter != NULL) {
        mrp_free(w->filter->path);
        mrp_free(w->filter->comm);
        mrp_free(w->filter);
    }

    mrp_free(w);

    return NULL;
}


static void free_watch(mrp_proc_watch_t *w)
{
    mrp_debug("freeing watch %p", w);

    mrp_list_delete(&w->hook);

    if (w->filter != NULL) {
        mrp_free(w->filter->path);
        mrp_free(w->filter->comm);
        mrp_free(w->filter);
    }

    mrp_free(w);
}


static void del_watch(mrp_proc_watch_t *w)
{
    mrp_debug("deleting watch %p", w);

    if (w->events != 0) {
        if (w->events & MRP_PROC_IGNORE_THREADS)
            no_thrd--;

        nwatch--;

        w->events = 0;
    }

    if (mrp_unref_obj(w, refcnt))
        free_watch(w);

    netlink_update_filter();
}


static bool check_filter(struct proc_event *e, mrp_proc_filter_t *f)
{
    pid_t pid;

    if (f == NULL || f->pid == 0)
        return true;

#define EVENT_PID(_type, _field)                                \
    case PROC_EVENT_##_type: pid = e->event_data._field; break
    switch (e->what) {
        EVENT_PID(FORK    , fork.parent_pid     );
        EVENT_PID(EXEC    , exec.process_pid    );
        EVENT_PID(UID     , id.process_pid      );
        EVENT_PID(GID     , id.process_pid      );
        EVENT_PID(PTRACE  , ptrace.process_pid  );
        EVENT_PID(COMM    , comm.process_pid    );
        EVENT_PID(COREDUMP, coredump.process_pid);
        EVENT_PID(EXIT    , exit.process_pid    );
    default: return false;
    }
#undef EVENT_PID

    if (pid != f->pid)
        return false;
    else
        return true;
}


static mrp_proc_watch_t *check_recurse(struct proc_event *e, mrp_proc_watch_t *w)
{
    mrp_proc_filter_t f;
    struct stat       st;
    char              path[PATH_MAX];

    if (w->filter == NULL)
        return false;

    if (w->filter->pid != e->event_data.fork.parent_pid)
        return false;

    f = *w->filter;
    f.pid = e->event_data.fork.child_pid;

    snprintf(path, sizeof(path), "/proc/%u", f.pid);

    if (stat(path, &st) == 0) {
        mrp_debug("adding recursive watch for process %u", f.pid);
        return add_watch(w->ml, w->events, &f, w->cb, w->user_data, true);
    }
    else {
        mrp_debug("omitting recursive watch, process %u gone", f.pid);
        return NULL;
    }
}


static bool notify_watch(mrp_proc_watch_t *w, struct proc_event *event)
{
    mrp_proc_event_t e;

    if (!(w->events & event->what))
        return false;

    if (!check_filter(event, w->filter))
        return false;

    e.type = event->what;
    e.raw  = event;
    w->cb(w, &e, w->user_data);

    return true;
}


static void notify_watches(mrp_list_hook_t *list, struct proc_event *event)
{
    mrp_list_hook_t  *p, *n;
    mrp_proc_watch_t *w, *c;
    bool              handled;

    mrp_list_foreach(list, p, n) {
        w = mrp_list_entry(p, typeof(*w), hook);

        if (event->what == PROC_EVENT_FORK && (w->events & MRP_PROC_RECURSE)) {
            if ((c = check_recurse(event, w)) != NULL) {
                mrp_ref_obj(c, refcnt);
                handled = notify_watch(c, event);
                if (mrp_unref_obj(c, refcnt))
                    del_watch(c);
                else
                    netlink_update_filter();

                if (handled)
                    continue;
            }
        }

        mrp_ref_obj(w, refcnt);
        notify_watch(w, event);
        if (mrp_unref_obj(w, refcnt))
            del_watch(w);

        if (event->what == PROC_EVENT_EXIT && w->recursed) {
            if (w->filter->pid == event->event_data.exit.process_pid)
                del_watch(w);
        }
    }
}


static void netlink_event_cb(mrp_io_watch_t *w, int fd, mrp_io_event_t events,
                             void *user_data)
{
    struct sockaddr_nl  addr;
    socklen_t           alen;
    struct nlmsghdr    *hdr;
    struct cn_msg      *msg;
    struct proc_event  *raw;
    char                buf[4096];
    ssize_t             len;
    int                 cnt;

    MRP_UNUSED(w);
    MRP_UNUSED(user_data);

    mrp_debug("got event on netlink socket...");

    if (events & MRP_IO_EVENT_IN) {
        alen = sizeof(addr);
        while ((len = recvfrom(fd, buf, sizeof(buf), MSG_DONTWAIT,
                               (struct sockaddr *)&addr, &alen)) > 0) {
            if (addr.nl_pid != 0)
                continue;

            hdr = (struct nlmsghdr *)buf;
            cnt = 0;

            while (NLMSG_OK(hdr, (size_t)len)) {

                switch (hdr->nlmsg_type) {
                case NLMSG_NOOP:
                    break;

                case NLMSG_ERROR:
                    mrp_log_error("Netlink process connector socket error.");
                    errno = EIO;
                    return;

                case NLMSG_OVERRUN:
                    mrp_log_error("Netlink process connector socket overrun.");
                    errno = EIO;
                    /* should reset the error (probably close and reopen) */
                    return;

                default:
                    msg = (struct cn_msg *)NLMSG_DATA(hdr);
                    raw = (struct proc_event *)msg->data;

                    if (msg->id.idx == CN_IDX_PROC &&
                        msg->id.val == CN_VAL_PROC) {
                        netlink_dump_event(raw);
                        notify_watches(&watches, raw);
                    }
                    break;
                }

                hdr = NLMSG_NEXT(hdr, len);
                cnt++;

                if (cnt > 1) {
                    mrp_log_error("Can't handle multiple connector messages / "
                                  "netlink message.");
                    exit(1);
                }
            }
        }
    }
}


static int netlink_request(enum proc_cn_mcast_op req)
{
    struct nlmsghdr       *hdr;
    struct cn_msg         *msg;
    enum proc_cn_mcast_op *op;
    int                    msg_size = NLMSG_SPACE(sizeof(*msg) + sizeof(*op));
    uint8_t                buf[msg_size];

    memset(buf, 0, msg_size);
    hdr = (struct nlmsghdr *)buf;
    msg = (struct cn_msg   *)NLMSG_DATA(hdr);
    op  = (enum proc_cn_mcast_op *)&msg->data[0];

    hdr->nlmsg_len   = NLMSG_LENGTH(msg_size - sizeof(*hdr));
    hdr->nlmsg_seq   = nl_seq++;
    hdr->nlmsg_pid   = getpid();
    hdr->nlmsg_flags = NLM_F_REQUEST;
    hdr->nlmsg_type  = NLMSG_DONE;

    msg->seq         = nl_seq;
    msg->ack         = nl_seq;
    msg->id.idx      = CN_IDX_PROC;
    msg->id.val      = CN_VAL_PROC;
    msg->len         = sizeof(*op);
    *op              = req;

    if (send(nl_sock, hdr, msg_size, 0) < 0) {
        mrp_log_error("Failed to send netlink process connector request "
                      "(error %d: %s).", errno, strerror(errno));

        return -1;
    }

    return 0;
}


static int netlink_subscribe(void)
{
    if (netlink_request(PROC_CN_MCAST_LISTEN) == 0) {
        nl_ack = false;
        return TRUE;
    }
    else
        return FALSE;
}


#if 0
static int netlink_unsubscribe(void)
{
    if (netlink_request(PROC_CN_MCAST_IGNORE) == 0)
        return TRUE;
    else
        return FALSE;
}
#endif


static int netlink_open(mrp_mainloop_t *ml)
{
    int nonblk, cloexc, events;
    struct sockaddr_nl addr;

    if (nl_sock >= 0)
        return nl_sock;

    nl_sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);

    if (nl_sock < 0) {
        mrp_log_error("Failed to open connector netlink socket "
                      "(error %d: %s).", errno, strerror(errno));
        return -1;
    }

    nonblk = O_NONBLOCK;
    cloexc = FD_CLOEXEC;

    if (fcntl(nl_sock, F_SETFD, cloexc) < 0 ||
        fcntl(nl_sock, F_SETFL, nonblk) < 0) {
        mrp_log_error("Failed to set CLOEXEC or NONBLOCK flag on "
                      "connector netlink socket (error %d: %s).",
                      errno, strerror(errno));
        goto fail;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_pid    = getpid();
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = CN_IDX_PROC;

    if (bind(nl_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        mrp_log_error("Failed to bind connector netlink socket "
                      "(error %d: %s).", errno, strerror(errno));
        goto fail;
    }

    if (!netlink_subscribe())
        goto fail;

    events   = MRP_IO_EVENT_IN | MRP_IO_EVENT_HUP | MRP_IO_EVENT_ERR;
    nl_watch = mrp_add_io_watch(ml, nl_sock, events, netlink_event_cb, NULL);

    if (nl_watch == NULL) {
        mrp_log_error("Failed to create I/O watch for netlink socket.");
        goto fail;
    }

    return nl_sock;

 fail:
    netlink_close();
    return -1;
}


static void netlink_close(void)
{
    mrp_del_io_watch(nl_watch);
    nl_watch = NULL;
    close(nl_sock);
    nl_sock = -1;
}


static inline int bpf_ensure(bpf_t *b, int n)
{
    ptrdiff_t nip = b->ip - b->insns;

    if (nip + n < b->ninsn)
        return 0;

    if (mrp_reallocz(b->insns, b->ninsn, b->ninsn + n)) {
        b->ninsn += n;
        b->ip     = b->insns + nip;

        return 0;
    }
    else
        return -1;
}


static inline int bpf_trim(bpf_t *b, int ninsn)
{
    ptrdiff_t nip = b->ip - b->insns;

    if (b->ninsn < ninsn)
        return -1;

    if (b->ninsn == ninsn)
        return 0;

    if (mrp_reallocz(b->insns, b->ninsn, ninsn)) {
        b->ninsn = ninsn;
        b->ip    = b->insns + nip;

        return 0;
    }
    else
        return -1;
}


static inline void bpf_reset(bpf_t *b)
{
    mrp_free(b->insns);
    b->insns = NULL;
    b->ninsn = 0;
    b->ip    = NULL;
}


static inline int bpf_setip(bpf_t *b, int n)
{
    if (n <= b->ninsn) {
        b->ip = b->insns + n;
        return 0;
    }
    else
        return -1;
}


static inline int bpf_getip(bpf_t *b)
{
    return b->ip - b->insns;
}


static inline int bpf_copy(bpf_t *b, int from, int to)
{
    if (from < b->ninsn && to < b->ninsn) {
        b->insns[to] = b->insns[from];
        return 0;
    }
    else
        return -1;
}


#define BASEOFFS (NLMSG_LENGTH(0) + MRP_OFFSET(struct cn_msg, data))
#define TYPEOFFS (BASEOFFS + MRP_OFFSET(struct proc_event, what))
#define EVNTOFFS(_e, _m) (BASEOFFS + MRP_OFFSET(struct proc_event,      \
                                                event_data._e._m))
#define FORKPIDOFFS EVNTOFFS(fork, child_pid)
#define FORKTIDOFFS EVNTOFFS(fork, child_tgid)
#define PIDOFFS     EVNTOFFS(exec, process_pid)
#define TIDOFFS     EVNTOFFS(exec, process_tgid)

#define STMT(b, stmt, k) do {                                            \
        mrp_debug("@0x%4.4x: %s, 0x%x", (b)->ip - (b)->insns, #stmt, k); \
        if (bpf_ensure(b, 1) < 0) {                                      \
            mrp_log_error("Failed to allocate BPF code buffer.");        \
            exit(1);                                                     \
        }                                                                \
        *(b)->ip++ = (struct sock_filter)BPF_STMT(stmt, k);              \
    } while (0)


#define JUMP(b, test, k, jtrue, jfalse) do {                            \
        int offs = (b)->ip - (b)->insns;                                \
                                                                        \
        mrp_debug("@0x%4.4x: %s, 0x%x, $0x%x : @0x%x", offs,            \
                  #test, k, offs + jtrue + 1, offs + jfalse + 1);       \
        if (bpf_ensure(b, 1) < 0) {                                     \
            mrp_log_error("Failed to allocate BPF code buffer.");       \
            exit(1);                                                    \
        }                                                               \
        *(b)->ip++ = (struct sock_filter)BPF_JUMP(test, k,              \
                                                  jtrue, jfalse);       \
    } while (0)


static bool add_pid_filter(bpf_t *b, mrp_proc_watch_t *w,
                           mrp_proc_event_type_t *events, bool *byname,
                           bool *bypid)
{
    int pid;

    *events |= w->events;
    *byname |= w->filter && (w->filter->path || w->filter->comm);
    *bypid  &= w->filter && w->filter->pid;

    if (!*byname && *bypid) {
        pid = htonl(w->filter->pid);

        STMT(b, BPF_LD  | BPF_W   | BPF_ABS, PIDOFFS  ); /* load pid */
        JUMP(b, BPF_JMP | BPF_JEQ | BPF_K  , pid, 0, 1); /* match? */
        STMT(b, BPF_RET |           BPF_K  , -1       ); /* pass thru */
    }

    return (*byname || !*bypid);
}


static void netlink_update_filter(void)
{
    mrp_proc_event_type_t  events;
    mrp_list_hook_t       *p, *n;
    mrp_proc_watch_t      *w;
    bool                   byname, bypid;
    struct sock_fprog      prog;
    int                    nprfx, nskip, offs, isfork;
    bpf_t                  bpf;

    setsockopt(nl_sock, SOL_SOCKET, SO_DETACH_FILTER, NULL, 0);

    if (!nl_ack)
        return;

    events = 0;
    byname = false;
    bypid  = true;
    nskip  = 0;

    memset(&prog, 0, sizeof(prog));
    memset(&bpf , 0, sizeof(bpf));

    /*
     * First generate a filter block to ignore events of a type in which
     * we have no interest. We do this by masking the type of the event
     * with the mask of all events of interest and checking that the result
     * is non-zero.
     */

    STMT(&bpf, BPF_LD  | BPF_W   | BPF_ABS, TYPEOFFS     ); /* A = type */
    STMT(&bpf, BPF_AND | BPF_ALU | BPF_K  , htonl(events)); /* A &= events */
    JUMP(&bpf, BPF_JMP | BPF_JEQ | BPF_K  , 0x0, 0, 1    ); /* if A != 0 */
    STMT(&bpf, BPF_RET |           BPF_K  , 0x0          ); /* filter out */

    /*
     * At this point if we're still processing, we have an event of type
     * we're interested in.
     *
     * Next, if we have the luxury to ignore threads we generate a filter
     * block to ignore events for threads. We do this by checking if the
     * pid and tgid are equal (for threads they are different). We need to
     * generate two sub-blocks because the pid and tgid are at different
     * offsets for a fork than for the rest of the events. So first we
     * check whether the event is a fork and then execute the appropriate
     * block.
     *
     * At this point we have the event type readily available in A.
     */

    if (no_thrd >= nwatch) {
        /* check type and jump to type-specific thread check */
        isfork = htonl(PROC_EVENT_FORK);
        JUMP(&bpf, BPF_JMP | BPF_JEQ | BPF_K, isfork, 0, 5);   /* a fork ? */

        /* check pid == tgid for forks */
        STMT(&bpf, BPF_LD   | BPF_W   | BPF_ABS, FORKPIDOFFS); /* A = pid */
        STMT(&bpf, BPF_MISC | BPF_TAX          , 0          ); /* X = A */
        STMT(&bpf, BPF_LD   | BPF_W   | BPF_ABS, FORKTIDOFFS); /* A = tid */
        JUMP(&bpf, BPF_JMP  | BPF_JEQ | BPF_X  , 0,  6, 0   ); /* if A == X */
        STMT(&bpf, BPF_RET  |           BPF_K  , 0x0        ); /* filter out */
        /* otherwise skip over the next block for non-forks */

        /* check pid == tgid for non-forks */
        STMT(&bpf, BPF_LD   | BPF_W   | BPF_ABS, PIDOFFS    ); /* A = pid */
        STMT(&bpf, BPF_MISC | BPF_TAX          , 0          ); /* X = A */
        STMT(&bpf, BPF_LD   | BPF_W   | BPF_ABS, TIDOFFS    ); /* A = tid */
        JUMP(&bpf, BPF_JMP  | BPF_JEQ | BPF_X  , 0,  1, 0   ); /* if A == X */
        STMT(&bpf, BPF_RET  |           BPF_K  , 0x0        ); /* filter out */
    }

    nprfx = bpf_getip(&bpf);

    /*
     * Next, if we are interested only in processes with known pids,
     * we generate filters to pass events for these pids thru. If
     * we encounter a filter that is not for a particular pid, we
     * remove all pid-specific filters.
     */

    mrp_list_foreach(&watches, p, n) {
        w = mrp_list_entry(p, typeof(*w), hook);

        if (add_pid_filter(&bpf, w, &events, &byname, &bypid))
            bpf_trim(&bpf, nprfx);
    }

    mrp_debug("event mask: 0x%x, filter-by-pid: %s, filter-by-name: %s",
              events, bypid ? "yes" : "no", byname ? "yes" : "no");

    /*
     * If we have no events of interest whatsoever, we can close
     * the socket for now. It will be later reopened if necessary
     * when a new process watch is created.
     */

    if (!events) {
        netlink_close();
        goto out;
    }

    /*
     * Now that we know the full set of events we're interested in,
     * patch the initially generated instruction with the right mask.
     */

    offs = bpf_getip(&bpf);
    bpf_setip(&bpf, 1);
    STMT(&bpf, BPF_AND | BPF_ALU | BPF_K, htonl(events)); /* patch mask */
    bpf_setip(&bpf, offs);
    offs = bpf_getip(&bpf);

    /*
     * If it turns out that we are interested in the full set of events,
     * there is no point in checking the type. Howver, if we do also thread
     * filtering, we'll still need the event type in A to check for forks
     * do the special special pid/tgid treatments. Otherwise we can skip the
     * whole initial type-checking block.
     *
     * Patch up things accordingly here if needed.
     */

    if (events == MRP_PROC_EVENT_ALL) {
        if (no_thrd >= nwatch) {
            bpf_copy(&bpf, 0, 3);                  /* move type loading */
            nskip = 3;                             /* ignore the rest */
        }
        else
            nskip = 4;                             /* ignore the whole block */
    }

    /*
     * Finally, if we have a filter generate the final instruction to
     * accept or reject messages. If we filter only for particular pids,
     * we need to reject messages here as we have already accepted matching
     * messages in the corresponding pid-specific block. If we don't filter
     * for any particular pids, we accept all messages here.
     */

    if (bpf.ninsn > 0) {
        STMT(&bpf, BPF_RET | BPF_K, bypid ? 0 : -1);

        prog.filter = bpf.insns + nskip;
        prog.len    = bpf.ninsn - nskip;

        mrp_debug("constructed socket filter has %d instructions", bpf.ninsn);

        if (setsockopt(nl_sock, SOL_SOCKET,
                       SO_ATTACH_FILTER, &prog, sizeof(prog)) < 0)
            mrp_log_error("Failed to install netlink socket filter (%d: %s).",
                          errno, strerror(errno));
    }

 out:
    bpf_reset(&bpf);
    return;
}

#undef STMT
#undef JUMP


static void netlink_dump_event(struct proc_event *e)
{
    char exe[PATH_MAX], bin[PATH_MAX];

    switch (e->what) {
    case PROC_EVENT_NONE:
        mrp_debug("process connector subscription ACK'ed");
        nl_ack = true;
        netlink_update_filter();
        break;

    case PROC_EVENT_FORK:
        mrp_debug("%u/%u forked child %u/%u", e->event_data.fork.parent_pid,
                  e->event_data.fork.parent_tgid, e->event_data.fork.child_pid,
                  e->event_data.fork.child_tgid);
        break;

    case PROC_EVENT_EXEC:
        snprintf(exe, sizeof(exe),
                 "/proc/%u/exe", e->event_data.exec.process_pid);
        if (readlink(exe, bin, sizeof(bin)) <= 0) {
            bin[0] = '?';
            bin[1] = '\0';
        }
        mrp_debug("%u/%u exec'd image %s", e->event_data.exec.process_pid,
                  e->event_data.exec.process_tgid, bin);
        break;

    case PROC_EVENT_UID:
        mrp_debug("%u/%u has changed its user ID %u -> %u",
                  e->event_data.id.process_pid, e->event_data.id.process_tgid,
                  e->event_data.id.r.ruid, e->event_data.id.e.euid);
        break;

    case PROC_EVENT_GID:
        mrp_debug("%u/%u has changed its group ID %u -> %u",
                  e->event_data.id.process_pid, e->event_data.id.process_tgid,
                  e->event_data.id.r.rgid, e->event_data.id.e.egid);
        break;

    case PROC_EVENT_SID:
        mrp_debug("%u/%u has changed its session",
                  e->event_data.id.process_pid, e->event_data.id.process_tgid);
        break;

    case PROC_EVENT_PTRACE:
        mrp_debug("%u/%u is now being traced by %u/%u",
                  e->event_data.ptrace.process_pid,
                  e->event_data.ptrace.process_tgid,
                  e->event_data.ptrace.tracer_pid,
                  e->event_data.ptrace.tracer_tgid);
        break;

    case PROC_EVENT_COREDUMP:
        mrp_debug("%u/%u has dumped core",
                  e->event_data.coredump.process_pid,
                  e->event_data.coredump.process_tgid);
        break;

    case PROC_EVENT_EXIT:
        mrp_debug("%u/%u has exited (code %u, signal %d (%s))",
                  e->event_data.exit.process_pid,
                  e->event_data.exit.process_tgid,
                  WEXITSTATUS(e->event_data.exit.exit_code),
                  WIFSIGNALED(e->event_data.exit.exit_code) ?
                  e->event_data.exit.exit_signal : 0,
                  WIFSIGNALED(e->event_data.exit.exit_code) &&
                  (int)e->event_data.exit.exit_signal > 0 ?
                  strsignal(e->event_data.exit.exit_signal) :
                  "<none>");
        break;

    case PROC_EVENT_COMM:
        mrp_debug("%u/%u has set its name to '%s'",
                  e->event_data.comm.process_pid,
                  e->event_data.comm.process_tgid,
                  e->event_data.comm.comm);
        break;

    default:
        mrp_debug("Received process connector event 0x%x.", e->what);
    }
}


/*
 * scan state (used to collect events during a scan of /proc)
 */

typedef struct {
    mrp_proc_event_type_t  mask;         /* events of interest */
    mrp_list_hook_t       *events;       /* synthetic events */
    pid_t                  ppid;         /* parent PID */
    uid_t                  puid;         /*        user ID */
    gid_t                  pgid;         /*        group ID */
    const char            *pexe;         /*        binary */
    const char            *pcomm;        /*        name */
    pid_t                  pid;          /* process/task PID */
    uid_t                  uid;          /*        user ID */
    gid_t                  gid;          /*        group ID */
    const char            *exe;          /*        binary */
    const char            *comm;         /*        name */
} scan_t;

static void generate_events(scan_t *scan, const char *pidstr);


static int scan_cb(const char *entry, mrp_dirent_type_t type, void *user_data)
{
    scan_t *scan = (scan_t *)user_data;

    MRP_UNUSED(type);

    generate_events(scan, entry);

    return TRUE;
}


static void scan_processes(mrp_list_hook_t *list, mrp_proc_event_type_t events)
{
#define PROC_ENTRY "regex:[0-9][0-9]*$"
    scan_t scan = {
        .mask    = events,
        .events  = list,
    };
    mrp_list_init(list);
    mrp_scan_dir("/proc", PROC_ENTRY, MRP_DIRENT_DIR, scan_cb, &scan);
#undef PROC_ENTRY
}


static void deliver_events(mrp_proc_watch_t *w, mrp_list_hook_t *events)
{
    mrp_list_hook_t *p, *n;
    synthev_t       *e;

    mrp_list_foreach(events, p, n) {
        e = mrp_list_entry(p, typeof(*e), hook);

        mrp_debug("delivering synthetic event %p", e);

        mrp_ref_obj(w, refcnt);
        notify_watch(w, &e->event);
        if (mrp_unref_obj(w, refcnt))
            del_watch(w);
    }
}


static void free_events(mrp_list_hook_t *events)
{
    mrp_list_hook_t *p, *n;
    synthev_t       *e;

    mrp_list_foreach(events, p, n) {
        e = mrp_list_entry(p, typeof(*e), hook);
        mrp_list_delete(&e->hook);
        mrp_free(e);
    }
}


static const char *find_tag(const char *buf, const char *tag)
{
    int n;

    if (buf == NULL)
        return NULL;

    n = strlen(tag);

    while (buf) {
        if (!strncmp(buf, tag, n) && buf[n] == ':') {
            buf += n + 1;

            while (*buf == ' ' || *buf == '\t')
                buf++;

            return buf;
        }

        buf = strchr(buf, '\n');

        if (buf != NULL)
            buf++;
    }

    return NULL;
}


static int get_process_status(pid_t pid, char *exep, size_t exel,
                              char *namep, size_t namel, pid_t *ppidp,
                              uid_t *uidp, gid_t *gidp)
{
    char        path[64], status[4096];
    const char *p, *tag, *end;
    int     fd;
    ssize_t len;

    if (exep != NULL) {
        snprintf(path, sizeof(path), "/proc/%u/exe", pid);
        len = readlink(path, exep, exel - 1);

        if (len >= 0)
            exep[len] = '\0';
        else
            return -1;
    }

    snprintf(path, sizeof(path), "/proc/%u/status", pid);
    fd = open(path, O_RDONLY);

    if (fd < 0)
        return -1;

    len = read(fd, status, sizeof(status) - 1);

    if (len < 0)
        return -1;
    else
        status[len] = '\0';

    p   = status;
    tag = find_tag(p, "Name");
    if (tag == NULL || (end = strchr(tag, '\n')) == NULL)
        return -1;
    len = end - tag;
    p   = end;

    if (namep != NULL && len > 0 && len < (int)namel - 1) {
        strncpy(namep, tag, len);
        namep[len] = '\0';
    }

    tag = find_tag(p, "PPid");
    if (tag == NULL || (end = strchr(tag, '\n')) == NULL)
        return -1;
    len = end - tag;
    p   = end;

    if (ppidp != NULL)
        *ppidp = (pid_t)strtoul(tag, NULL, 10);

    tag = find_tag(p, "Uid");
    if (tag == NULL || (end = strchr(tag, '\n')) == NULL)
        return -1;
    len = end - tag;
    p   = end;

    if (uidp != NULL)
        *uidp = (uid_t)strtoul(tag, NULL, 10);

    tag = find_tag(p, "Gid");
    if (tag == NULL || (end = strchr(tag, '\n')) == NULL)
        return -1;
    len = end - tag;

    if (gidp != NULL)
        *gidp = (uid_t)strtoul(tag, NULL, 10);

    return 0;
}


static void post_task_events(mrp_list_hook_t *list, mrp_proc_event_type_t events,
                             pid_t ppid, const char *pexe, const char *pcomm,
                             uid_t puid, gid_t pgid, pid_t pid, pid_t tgid,
                             const char *exe, const char *comm, uid_t uid,
                             gid_t gid)
{
    struct proc_event *e;
    synthev_t         *evt;
    char               tcomm[32];
    bool               chg;

    mrp_debug("  generating events for task %u/%u", pid, tgid);

    if (pid != tgid && (events & MRP_PROC_IGNORE_THREADS))
        return;

    /* generate pseudo-fork event */
    if (events & MRP_PROC_EVENT_FORK) {
        evt = mrp_allocz(sizeof(*evt));

        if (evt == NULL)
            return;

        mrp_list_init(&evt->hook);

        e = &evt->event;
        e->what = PROC_EVENT_FORK;
        e->event_data.fork.parent_pid  = ppid;
        e->event_data.fork.parent_tgid = tgid;
        e->event_data.fork.child_pid   = pid;
        e->event_data.fork.child_tgid  = tgid;

        mrp_list_append(list, &evt->hook);
    }

    /* generate pseudo-exec event if necessary */
    if (pid == tgid && strcmp(pexe, exe) && (events & MRP_PROC_EVENT_EXEC)) {
        evt = mrp_allocz(sizeof(*evt));

        if (evt == NULL)
            return;

        mrp_list_init(&evt->hook);

        e = &evt->event;
        e->what = PROC_EVENT_EXEC;
        e->event_data.exec.process_pid  = pid;
        e->event_data.exec.process_tgid = tgid;

        mrp_list_append(list, &evt->hook);
    }

    /* generate pseudo-uid event if necessary */
    if (uid != puid && (events & MRP_PROC_EVENT_UID)) {
        evt = mrp_allocz(sizeof(*evt));

        if (evt == NULL)
            return;

        mrp_list_init(&evt->hook);

        e = &evt->event;
        e->what = PROC_EVENT_UID;
        e->event_data.id.r.ruid = puid;
        e->event_data.id.e.euid = uid;

        mrp_list_append(list, &evt->hook);
    }

    /* generate pseudo-gid event if necessary */
    if (gid != pgid && (events & MRP_PROC_EVENT_GID)) {
        evt = mrp_allocz(sizeof(*evt));

        if (evt == NULL)
            return;

        mrp_list_init(&evt->hook);

        e = &evt->event;
        e->what = PROC_EVENT_GID;
        e->event_data.id.r.rgid = pgid;
        e->event_data.id.e.egid = gid;

        mrp_list_append(list, &evt->hook);
    }

    /* generate pseudo-comm event if necessary */
    if (events & MRP_PROC_EVENT_COMM) {
        /* for the main thread we have the comm, for others we fetch it */
        chg = false;
        if (pid != tgid) {
            if (get_process_status(tgid, NULL, 0, tcomm, sizeof(tcomm),
                                   NULL, NULL, NULL) == 0) {
                chg  = !strcmp(tcomm, pcomm);
                comm = tcomm;
            }
            else
                chg = false;
        }
        else
            chg = !strcmp(pcomm, comm);

        if (chg) {
            evt = mrp_allocz(sizeof(*evt));

            if (evt == NULL)
                return;

            mrp_list_init(&evt->hook);

            e = &evt->event;
            e->what = PROC_EVENT_COMM;
            e->event_data.comm.process_pid  = pid;
            e->event_data.comm.process_tgid = tgid;
            strcpy(e->event_data.comm.comm, comm);

            mrp_list_append(list, &evt->hook);
        }
    }
}


static int task_cb(const char *entry, mrp_dirent_type_t type, void *user_data)
{
    scan_t *scan = (scan_t *)user_data;
    pid_t   task = (pid_t)strtoul(entry, NULL, 10);

    MRP_UNUSED(type);

    post_task_events(scan->events, scan->mask,
                     scan->ppid, scan->pexe, scan->pcomm, scan->puid, scan->pgid,
                     scan->pid, task, scan->exe, scan->comm, scan->uid,
                     scan->gid);

    return TRUE;
}


static void generate_events(scan_t *scan, const char *pidstr)
{
#define TASK_ENTRY "regex:[0-9][0-9]*$"
    char exe[PATH_MAX], pexe[PATH_MAX], comm[64], pcomm[64], task[256];

    mrp_debug("generating events for process %s", pidstr);

    scan->pid   = strtol(pidstr, NULL, 10);
    scan->exe   = exe;
    scan->comm  = comm;
    scan->pexe  = pexe;
    scan->pcomm = pcomm;

    if (get_process_status(scan->pid, exe, sizeof(exe),comm, sizeof(comm),
                           &scan->ppid, &scan->uid, &scan->gid) < 0 ||
        get_process_status(scan->ppid, pexe, sizeof(pexe), pcomm, sizeof(pcomm),
                           NULL, &scan->puid, &scan->pgid) < 0)
        return;

    snprintf(task, sizeof(task), "/proc/%s/task", pidstr);
    mrp_scan_dir(task, TASK_ENTRY, MRP_DIRENT_DIR, task_cb, scan);
#undef TASK_ENTRY
}
