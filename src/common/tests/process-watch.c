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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>

#include <murphy/common/macros.h>
#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/process-watch.h>

typedef struct {
    mrp_proc_event_type_t events;          /* events to track */
    pid_t                 pid;             /* pid to track or 0 for all */
    bool                  follow_children; /* whether to follow children */
    bool                  ignore_threads;  /* whether to report thread events */
    bool                  existing;        /* whether to report synthetic events
                                            * for existing processes/threads */
    mrp_mainloop_t       *ml;
    mrp_proc_watch_t     *w;

    int                   log_mask;
    const char           *log_target;
} config_t;


static void config_set_defaults(config_t *cfg)
{
    mrp_clear(cfg);

    cfg->events     = MRP_PROC_EVENT_ALL;
    cfg->log_mask   = MRP_LOG_UPTO(MRP_LOG_INFO);
    cfg->log_target = MRP_LOG_TO_STDERR;
}


static void print_usage(const char *argv0, int exit_code, const char *fmt, ...)
{
    va_list ap;

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        printf("\n");
        va_end(ap);
    }

    printf("usage: %s [options]\n\n"
           "The possible options are:\n"
           "  -p, --process <pid>            which process to track\n"
           "  -e, --events <event-list>      which events to print/track\n"
           "  -C, --follow-children          follow children processes\n"
           "  -T, --ignore-threads           ignore events for/from threads\n"
           "  -l, --log-level=LEVELS         logging level to use\n"
           "      LEVELS is a comma separated list of info, error and warning\n"
           "  -t, --log-target=TARGET        log target to use\n"
           "      TARGET is one of stderr,stdout,syslog, or a logfile path\n"
           "  -v, --verbose                  increase logging verbosity\n"
           "  -d, --debug [site]             enable debug messages\n"
           "  -h, --help                     show help on usage\n",
           argv0);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


int parse_events(const char *argv0, const char *events)
{
    const char *p;
    mrp_proc_event_type_t mask;

    mask = 0;

    if (events == NULL)
        return 0;

    p = events;
    while (p && *p) {
#       define MATCHES(s, l) (!strcmp(s, l) ||                          \
                              !strncmp(s, l",", sizeof(l",") - 1))

        if (MATCHES(p, "none"))
            mask |= MRP_PROC_EVENT_NONE;
        else if (MATCHES(p, "fork"))
            mask |= MRP_PROC_EVENT_FORK;
        else if (MATCHES(p, "exec"))
            mask |= MRP_PROC_EVENT_EXEC;
        else if (MATCHES(p, "UID"))
            mask |= MRP_PROC_EVENT_UID;
        else if (MATCHES(p, "gid"))
            mask |= MRP_PROC_EVENT_GID;
        else if (MATCHES(p, "sid"))
            mask |= MRP_PROC_EVENT_SID;
        else if (MATCHES(p, "ptrace"))
            mask |= MRP_PROC_EVENT_PTRACE;
        else if (MATCHES(p, "comm") || MATCHES(p, "cmd"))
            mask |= MRP_PROC_EVENT_COMM;
        else if (MATCHES(p, "coredump") || MATCHES(p, "core"))
            mask |= MRP_PROC_EVENT_COREDUMP;
        else if (MATCHES(p, "exit"))
            mask |= MRP_PROC_EVENT_EXIT;
        else if (MATCHES(p, "all"))
            mask |= MRP_PROC_EVENT_ALL;
        else if (MATCHES(p, "children"))
            mask |= MRP_PROC_RECURSE;
        else if (MATCHES(p, "recurse") || MATCHES(p, "recursive"))
            mask |= MRP_PROC_RECURSE;
        else if (MATCHES(p, "scan-proc") || MATCHES(p, "scan"))
            mask |= MRP_PROC_SCAN_EXISTING;
        else if (MATCHES(p, "existing"))
            mask |= MRP_PROC_SCAN_EXISTING;
        else if (MATCHES(p, "ignore-threads") || MATCHES(p, "no-threads"))
            mask |= MRP_PROC_IGNORE_THREADS;
            else
            print_usage(argv0, -1, "invalid process event '%s'", p);

        if ((p = strchr(p, ',')) != NULL)
            p += 1;

#       undef MATCHES
    }

    return mask;
}


static void parse_cmdline(config_t *cfg, int argc, char **argv)
{
#define OPTIONS "p:e:CTl:t:vd:h"

    struct option options[] = {
        { "process"        , required_argument, NULL, 'p' },
        { "events"         , required_argument, NULL, 'e' },
        { "follow-children", no_argument      , NULL, 'C' },
        { "ignore-threads" , no_argument      , NULL, 'T' },
        { "log-level"      , required_argument, NULL, 'l' },
        { "log-target"     , required_argument, NULL, 't' },
        { "verbose"        , no_argument      , NULL, 'v' },
        { "debug"          , required_argument, NULL, 'd' },
        { "help"           , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    char *end;
    int   opt;

    config_set_defaults(cfg);

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'p':
            cfg->pid = (int)strtoul(optarg, &end, 10);
            if (end && *end)
                print_usage(argv[0], EINVAL,
                            "invalid runtime length '%s'.", optarg);
            break;

        case 'e':
            cfg->events = parse_events(argv[0], optarg);
            break;

        case 'v':
            cfg->log_mask <<= 1;
            cfg->log_mask  |= 1;
            mrp_log_set_mask(cfg->log_mask);
            break;

        case 'l':
            cfg->log_mask = mrp_log_parse_levels(optarg);
            if (cfg->log_mask < 0)
                print_usage(argv[0], EINVAL, "invalid log level '%s'", optarg);
            mrp_log_set_mask(cfg->log_mask);
            break;

        case 't':
            cfg->log_target = mrp_log_parse_target(optarg);
            if (!cfg->log_target)
                print_usage(argv[0], EINVAL, "invalid log target '%s'", optarg);
            break;

        case 'd':
            cfg->log_mask |= MRP_LOG_MASK_DEBUG;
            mrp_debug_set_config(optarg);
            mrp_debug_enable(TRUE);
            break;

        case 'h':
            print_usage(argv[0], -1, "");
            exit(0);
            break;

        default:
            print_usage(argv[0], EINVAL, "invalid option '%c'", opt);
        }
    }

    mrp_log_set_target(cfg->log_target);
}


static void dump_fork_event(mrp_proc_watch_t *w, mrp_proc_event_t *event)
{
    struct proc_event *e = event->raw;

    printf("%p: fork by process %u/%u -> child: %u/%u\n", w,
           e->event_data.fork.parent_pid, e->event_data.fork.parent_tgid,
           e->event_data.fork.child_pid, e->event_data.fork.child_tgid);
}


static void dump_exit_event(mrp_proc_watch_t *w, mrp_proc_event_t *event)
{
    struct proc_event *e = event->raw;

    printf("%p: exit of process %u/%u (code: %d, signal: %s)\n", w,
           e->event_data.exit.process_pid, e->event_data.exit.process_tgid,
           WEXITSTATUS(e->event_data.exit.exit_code),
           WIFSIGNALED(e->event_data.exit.exit_code) ?
           strsignal(WTERMSIG(e->event_data.exit.exit_signal)) : "none");
}


static void dump_exec_event(mrp_proc_watch_t *w, mrp_proc_event_t *event)
{
    struct proc_event *e = event->raw;
    char               exe[PATH_MAX], img[PATH_MAX];
    ssize_t            len;

    snprintf(exe, sizeof(exe), "/proc/%u/exe", e->event_data.exec.process_pid);

    if ((len = readlink(exe, img, sizeof(img) - 1)) > 0)
        img[len] = '\0';
    else {
        img[0] = '?';
        img[1] = '\0';
    }

    printf("%p: process %u/%u executed a new image (%s)\n", w,
           e->event_data.exec.process_pid,e->event_data.exec.process_tgid, img);
}


static void process_event(mrp_proc_watch_t *w, mrp_proc_event_t *e,
                          void *user_data)
{
    MRP_UNUSED(w);
    MRP_UNUSED(user_data);

    switch (e->type) {
    case MRP_PROC_EVENT_FORK:
        dump_fork_event(w, e);
        break;

    case MRP_PROC_EVENT_EXIT:
        dump_exit_event(w, e);
        break;

    case MRP_PROC_EVENT_EXEC:
        dump_exec_event(w, e);
        break;

    default:
        printf("event 0x%x\n", e->raw->what);
        break;
    }
}


int main(int argc, char *argv[])
{
    config_t          cfg;
    mrp_proc_filter_t pidf, *f;


    parse_cmdline(&cfg, argc, argv);

    printf("event mask: 0x%x\n", cfg.events);
    printf("       pid: %d\n", cfg.pid);

    if (cfg.pid != 0) {
        mrp_clear(&pidf);
        pidf.pid = cfg.pid;
        f = &pidf;
    }
    else
        f = NULL;

    cfg.ml = mrp_mainloop_create();
    cfg.w  = mrp_add_proc_watch(cfg.ml, cfg.events, f, process_event, &cfg);

    mrp_mainloop_run(cfg.ml);

    return 0;
}
