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

#include <errno.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>

#include <murphy/common/macros.h>
#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/core/lua-bindings/murphy.h>

#include "process-watch.h"

/*
 * process watch object
 */

#define PROCESS_WATCH_LUA_CLASS MRP_LUA_CLASS(process_watch, lua)
#define RO                      MRP_LUA_CLASS_READONLY
#define NOINIT                  MRP_LUA_CLASS_NOINIT
#define NOFLAGS                 MRP_LUA_CLASS_NOFLAGS
#define setmember               process_watch_setmember
#define getmember               process_watch_getmember

static int process_watch_no_constructor(lua_State *L);
static void process_watch_destroy(void *data);
static void process_watch_changed(void *data, lua_State *L, int member);
static ssize_t process_watch_tostring(mrp_lua_tostr_mode_t mode, char *buf,
                                      size_t size, lua_State *L, void *data);
static int process_watch_setmember(void *data, lua_State *L, int member,
                                   mrp_lua_value_t *v);
static int process_watch_getmember(void *data, lua_State *L, int member,
                                   mrp_lua_value_t *v);
static int process_watch_delete(lua_State *L);

static int setup_filter(process_watch_lua_t *w, lua_State *L, int filterref);


MRP_LUA_METHOD_LIST_TABLE(process_watch_methods,
    MRP_LUA_METHOD_CONSTRUCTOR(process_watch_no_constructor)
    MRP_LUA_METHOD(delete, process_watch_delete));

MRP_LUA_METHOD_LIST_TABLE(process_watch_overrides,
    MRP_LUA_OVERRIDE_CALL(process_watch_no_constructor));

MRP_LUA_MEMBER_LIST_TABLE(process_watch_members,
    MRP_LUA_CLASS_INTEGER("events", 0, setmember, getmember, RO)
    MRP_LUA_CLASS_ANY    ("filter", 0, setmember, getmember, RO)
    MRP_LUA_CLASS_ANY    ("notify", 0, setmember, getmember, RO));

MRP_LUA_DEFINE_CLASS(process_watch, lua, process_watch_lua_t,
                     process_watch_destroy, process_watch_methods,
                     process_watch_overrides, process_watch_members, NULL,
                     process_watch_changed, process_watch_tostring, NULL,
                     MRP_LUA_CLASS_EXTENSIBLE | MRP_LUA_CLASS_PRIVREFS);

MRP_LUA_CLASS_CHECKER(process_watch_lua_t, process_watch_lua,
                      PROCESS_WATCH_LUA_CLASS);

typedef enum {
    PROCESS_WATCH_MEMBER_EVENTS,
    PROCESS_WATCH_MEMBER_FILTER,
    PROCESS_WATCH_MEMBER_NOTIFY
} process_watch_member_t;


/*
 * process event object
 */

#define PROCESS_EVENT_CLASS MRP_LUA_CLASS_SIMPLE(ProcessEvent)

typedef struct {
    mrp_proc_event_t *event;
} ProcessEvent_t;

typedef ProcessEvent_t process_event_lua_t;

static int event_no_constructor(lua_State *L);
static void *event_create(lua_State *L, mrp_proc_event_t *e);
static int event_setfield(lua_State *L);
static int event_getfield(lua_State *L);
static void event_destroy(void *data);
static process_event_lua_t *event_check(lua_State *L, int idx);
static bool register_event_class(lua_State *L);

MRP_LUA_CLASS_DEF_SIMPLE(
    ProcessEvent,
    ProcessEvent_t,
    event_destroy,
    MRP_LUA_METHOD_LIST(
        MRP_LUA_METHOD_CONSTRUCTOR(event_no_constructor)),
    MRP_LUA_METHOD_LIST(
        MRP_LUA_OVERRIDE_CALL(event_no_constructor)
        MRP_LUA_OVERRIDE_GETFIELD(event_getfield)
        MRP_LUA_OVERRIDE_SETFIELD(event_setfield)));

static int process_watch_no_constructor(lua_State *L)
{
    return luaL_error(L, "trying to create a process watch via constructor.");
}


static void process_watch_notify(process_watch_lua_t *w, lua_State *L,
                                 mrp_proc_event_t *e)
{
    mrp_funcbridge_value_t args[3], rv;
    char                            rt;

    if (L == NULL || w->notify == NULL)
        return;

    mrp_debug("notifying process watch %p", w);

    args[0].pointer = w;
    args[1].pointer = event_create(L, e);

    if (!mrp_funcbridge_call_from_c(L, w->notify, "OO", &args[0], &rt, &rv)) {
        mrp_log_error("Failed to notify process watch %p.", w);
        mrp_free((char *)rv.string);
    }
}


static void event_cb(mrp_proc_watch_t *pw, mrp_proc_event_t *e, void *user_data)
{
    process_watch_lua_t *w = (process_watch_lua_t *)user_data;

    MRP_UNUSED(pw);

    mrp_debug("got notification (0x%x) for process watch %p", e->type, w);

    process_watch_notify(w, mrp_lua_get_lua_state(), e);
}


process_watch_lua_t *process_watch_create(sysmon_lua_t *sm, lua_State *L)
{
    process_watch_lua_t *w;
    mrp_context_t       *ctx;
    mrp_proc_filter_t   *f;
    char                 e[256];

    luaL_checktype(L, 2, LUA_TTABLE);

    w = (process_watch_lua_t *)mrp_lua_create_object(L, PROCESS_WATCH_LUA_CLASS,
                                                     NULL, 0);

    mrp_list_init(&w->hook);
    w->sysmon     = sm;
    w->watchref   = LUA_NOREF;
    w->filterref  = LUA_NOREF;
    w->filter.uid = -1;
    w->filter.gid = -1;

    if (mrp_lua_init_members(w, L, 2, e, sizeof(e)) != 1) {
        luaL_error(L, "failed to initialize process watch (error: %s)",
                   *e ? e : "<unknown error>");
        return NULL;
    }

    if (w->notify == NULL) {
        luaL_error(L, "process watch notification callback not set");
        return NULL;
    }

    if (w->filter.pid != 0 || w->filter.path || w->filter.comm ||
        w->filter.uid != (uid_t)-1 || w->filter.gid != (gid_t)-1)
        f = &w->filter;
    else
        f = NULL;

    ctx  = mrp_lua_get_murphy_context();
    w->w = mrp_add_proc_watch(ctx->ml, w->mask, f, event_cb, w);

    if (w->w != NULL)
        return w;
    else {
        luaL_error(L, "failed to create process watch");
        return NULL;
    }
}


static void process_watch_destroy(void *data)
{
    mrp_debug("process watch %p destroyed", data);
}


static int process_watch_delete(lua_State *L)
{
    process_watch_lua_t *w = process_watch_lua_check(L, 1);

    mrp_lua_object_unref_value(w, L, w->filterref);
    w->filterref = LUA_NOREF;

    mrp_del_proc_watch(w->w);
    sysmon_del_process_watch(w->sysmon, w);

    return 0;
}


static void process_watch_changed(void *data, lua_State *L, int member)
{
    MRP_UNUSED(data);
    MRP_UNUSED(L);
    MRP_UNUSED(member);
}


static int process_watch_setmember(void *data, lua_State *L, int member,
                                   mrp_lua_value_t *v)
{
    process_watch_lua_t *w = (process_watch_lua_t *)data;

    switch (member) {
    case PROCESS_WATCH_MEMBER_EVENTS:
        w->mask = v->s32;
        return 1;

    case PROCESS_WATCH_MEMBER_FILTER:
        if (v->any != LUA_NOREF)
            return setup_filter(w, L, v->any);
        else
            return 1;

    case PROCESS_WATCH_MEMBER_NOTIFY:
        if (!mrp_lua_object_deref_value(w, L, v->any, false))
            return 0;
        switch (lua_type(L, -1)) {
        case LUA_TFUNCTION:
            w->notify = mrp_funcbridge_create_luafunc(L, -1);
            break;
        default:
            w->notify = NULL;
            break;
        }
        lua_pop(L, 1);
        mrp_lua_object_unref_value(w, L, v->any);

        return (w->notify != NULL ? 1 : 0);

    default:
        mrp_log_error("Can't set read-only process watch member #%d.", member);
        return 0;
    }
}


static int process_watch_getmember(void *data, lua_State *L, int member,
                                   mrp_lua_value_t *v)
{
    process_watch_lua_t *w = (process_watch_lua_t *)data;

    MRP_UNUSED(data);
    MRP_UNUSED(L);

    switch (member) {
    case PROCESS_WATCH_MEMBER_EVENTS:
        v->s32 = w->mask;
        return 1;

    case PROCESS_WATCH_MEMBER_FILTER:
        v->any = w->filterref;
        return 1;

    default:
        v->any = LUA_REFNIL;
        return 1;
    }
}


static ssize_t process_watch_tostring(mrp_lua_tostr_mode_t mode, char *buf,
                                      size_t size, lua_State *L, void *data)
{
    process_watch_lua_t *w = (process_watch_lua_t *)data;

    MRP_UNUSED(L);

    switch (mode & MRP_LUA_TOSTR_MODEMASK) {
    case MRP_LUA_TOSTR_LUA:
    default:
        return snprintf(buf, size, "{process watch %p}", w);
    }
}


int process_event_mask(lua_State *L, int idx)
{
    mrp_proc_event_type_t  mask;
    const char            *event, *name;
    int                    ktype, type, i;
    size_t                 len;

    mask = 0;
    MRP_LUA_FOREACH_ALL(L, i, idx, ktype, name, len) {
        if (ktype != LUA_TNUMBER || (type = lua_type(L, -1)) != LUA_TSTRING) {
            mrp_log_warning("ignoring invalid event (0x%x: 0x%x)", ktype, type);
            continue;
        }
        else
            event = lua_tolstring(L, -1, &len);

        switch (len) {
        case 3:
            if (!strcmp(event, "uid"))
                mask |= MRP_PROC_EVENT_UID;
            else if (!strcmp(event, "gid"))
                mask |= MRP_PROC_EVENT_GID;
            else if (!strcmp(event, "sid"))
                mask |= MRP_PROC_EVENT_SID;
            else
                goto unknown;
            break;

        case 4:
            if (!strcmp(event, "none"))
                mask |= MRP_PROC_EVENT_NONE;
            else if (!strcmp(event, "fork"))
                mask |= MRP_PROC_EVENT_FORK;
            else if (!strcmp(event, "exec"))
                mask |= MRP_PROC_EVENT_EXEC;
            else if (!strcmp(event, "exit"))
                mask |= MRP_PROC_EVENT_EXIT;
            else if (!strcmp(event, "user"))
                mask |= MRP_PROC_EVENT_UID;
            else if (!strcmp(event, "comm"))
                mask |= MRP_PROC_EVENT_COMM;
            else if (!strcmp(event, "core"))
                mask |= MRP_PROC_EVENT_COREDUMP;
            else
                goto unknown;
            break;

        case 5:
            if (!strcmp(event, "trace"))
                mask |= MRP_PROC_EVENT_PTRACE;
            else if (!strcmp(event, "group"))
                mask |= MRP_PROC_EVENT_GID;
            else
                goto unknown;
            break;

        case 6:
            if (!strcmp(event, "ptrace"))
                mask |= MRP_PROC_EVENT_PTRACE;
            else
                goto unknown;
            break;

        case 7:
            if (!strcmp(event, "session"))
                mask |= MRP_PROC_EVENT_SID;
            else if (!strcmp(event, "recurse"))
                mask |= MRP_PROC_RECURSE;
            else
                goto unknown;
            break;

        case 8:
            if (!strcmp(event, "coredump"))
                mask |= MRP_PROC_EVENT_COREDUMP;
            else
                goto unknown;
            break;

        case 9:
            if (!strcmp(event, "nothreads"))
                mask |= MRP_PROC_IGNORE_THREADS;
            else
                goto unknown;
            break;

        case 13:
            if (!strcmp(event, "ignorethreads"))
                mask |= MRP_PROC_IGNORE_THREADS;
            else
                goto unknown;
            break;

        unknown:
        default:
            mrp_log_warning("ignoring unknown process event '%s'", event);
            break;
        }
    }

    lua_pushinteger(L, mask);
    return 1;
}


static int setup_filter(process_watch_lua_t *w, lua_State *L, int filterref)
{
    int         top = lua_gettop(L);
    int         ktype, i;
    const char *kname, *usr, *grp;
    size_t      klen, len;

    if (!mrp_lua_object_deref_value(w, L, filterref, false)) {
        mrp_log_error("Failed to dereference process watch filter table.");
        return 0;
    }

    MRP_LUA_FOREACH_ALL(L, i, top + 1, ktype, kname, klen) {
        if (ktype != LUA_TSTRING) {
            mrp_log_error("Invalid process watch filter (non-string key).");
            goto fail;
        }

        kname = lua_tolstring(L, -2, &len);

        if (!strcmp(kname, "pid") || !strcmp(kname, "process")) {
            if (lua_type(L, -1) != LUA_TNUMBER) {
                mrp_log_error("Invalid process watch filter pid.");
                goto fail;
            }

            w->filter.pid = lua_tointeger(L, -1);
            continue;
        }

        if (!strcmp(kname, "path")) {
            if (lua_type(L, -1) != LUA_TSTRING) {
                mrp_log_error("Invalid process watch filter path.");
                goto fail;
            }

            w->filter.path = mrp_strdup(lua_tostring(L, -1));
            continue;
        }

        if (!strcmp(kname, "comm") || !strcmp(kname, "name")) {
            if (lua_type(L, -1) != LUA_TSTRING) {
                mrp_log_error("Invalid process watch filter comm.");
                goto fail;
            }

            w->filter.comm = mrp_strdup(lua_tostring(L, -1));
            continue;
        }

        if (!strcmp(kname, "uid") || !strcmp(kname, "user")) {
            if (lua_type(L, -1) == LUA_TNUMBER) {
                w->filter.uid = lua_tointeger(L, -1);
                continue;
            }

            if (lua_type(L, -1) == LUA_TSTRING) {
                struct passwd *pw = getpwnam(usr = lua_tolstring(L, -1, &len));

                if (pw == NULL) {
                    mrp_log_error("Unknown process watch filter user '%s'.",
                                  usr);
                    goto fail;
                }

                w->filter.uid = pw->pw_uid;
                continue;
            }

            mrp_log_error("Invalid process watch filter user.");
            goto fail;
        }

        if (!strcmp(kname, "gid") || !strcmp(kname, "group")) {
            if (lua_type(L, -1) == LUA_TNUMBER) {
                w->filter.gid = lua_tointeger(L, -1);
                continue;
            }

            if (lua_type(L, -1) == LUA_TSTRING) {
                struct group *gr = getgrnam(grp = lua_tolstring(L, -1, &len));

                if (gr == NULL) {
                    mrp_log_error("Unknown process watch filter group '%s'.",
                                  grp);
                    goto fail;
                }

                w->filter.gid = gr->gr_gid;
                continue;
            }

            mrp_log_error("Invalid process watch filter group.");
            goto fail;
        }

        mrp_log_error("Invalid process watch filter field '%s'.", kname);
        goto fail;
    }

    w->filterref = mrp_lua_object_ref_value(w, L, top + 1);
    lua_settop(L, top);

    return 1;

 fail:
    lua_settop(L, top);
    return 0;
}


static bool register_event_class(lua_State *L)
{
    mrp_lua_create_object_class(L, PROCESS_EVENT_CLASS);

    return true;
}


static int event_no_constructor(lua_State *L)
{
    return luaL_error(L, "trying to create a process event via constructor.");
}


static void *event_create(lua_State *L, mrp_proc_event_t *event)
{
    static bool          eclass = false;
    process_event_lua_t *e;

    if (!eclass)
        if (!(eclass = register_event_class(L)))
            return NULL;

    e = (process_event_lua_t *)mrp_lua_create_object(L, PROCESS_EVENT_CLASS,
                                                     NULL, 0);

    if (e == NULL || (e->event = mrp_allocz(sizeof(*e->event))) == NULL) {
        mrp_log_error("Failed to allocate process event.");
        return NULL;
    }

    memcpy(e->event, event, sizeof(*e->event));

    return e;
}


static int event_setfield(lua_State *L)
{
    event_check(L, 1);
    return luaL_error(L, "trying to set field on read-only process event");
}


static int event_getfield(lua_State *L)
{
    process_event_lua_t *e = event_check(L, -2);
    const char          *name, *type;;

    if (lua_type(L, -1) != LUA_TSTRING)
        return luaL_error(L, "process event has only string fields");

    if (e->event == NULL) {
        lua_pushnil(L);
        return 1;
    }

    name = lua_tostring(L, -1);

    if (!strcmp(name, "type")) {
        switch (e->event->type) {
        case MRP_PROC_EVENT_NONE:     type = "<none>"   ; break;
        case MRP_PROC_EVENT_FORK:     type = "fork"     ; break;
        case MRP_PROC_EVENT_EXEC:     type = "exec"     ; break;
        case MRP_PROC_EVENT_EXIT:     type = "exit"     ; break;
        case MRP_PROC_EVENT_UID:      type = "uid"      ; break;
        case MRP_PROC_EVENT_GID:      type = "gid"      ; break;
        case MRP_PROC_EVENT_SID:      type = "sid"      ; break;
        case MRP_PROC_EVENT_PTRACE:   type = "ptrace"   ; break;
        case MRP_PROC_EVENT_COMM:     type = "comm"     ; break;
        case MRP_PROC_EVENT_COREDUMP: type = "coredump" ; break;
        default:                      type = "<unknown>"; break;
        }

        lua_pushstring(L, type);
        return 1;
    }

    if (!strcmp(name, "pid") || !strcmp(name, "process_pid")) {
        if (e->event->type != MRP_PROC_EVENT_FORK)
            lua_pushinteger(L, e->event->raw->event_data.exec.process_pid);
        else
            lua_pushnil(L);
        return 1;
    }

    if (!strcmp(name, "tgid") || !strcmp(name, "process_tgid")) {
        if (e->event->type != MRP_PROC_EVENT_FORK)
            lua_pushinteger(L, e->event->raw->event_data.exec.process_tgid);
        else
            lua_pushnil(L);
        return 1;
    }

    if (e->event->type == MRP_PROC_EVENT_FORK) {
        if (!strcmp(name, "parent_pid"))
            lua_pushinteger(L, e->event->raw->event_data.fork.parent_pid);
        else if (!strcmp(name, "parent_tgid"))
            lua_pushinteger(L, e->event->raw->event_data.fork.parent_tgid);
        else if (!strcmp(name, "child_pid"))
            lua_pushinteger(L, e->event->raw->event_data.fork.child_pid);
        else if (!strcmp(name, "child_tgid"))
            lua_pushinteger(L, e->event->raw->event_data.fork.child_tgid);
        else
            lua_pushnil(L);
        return 1;
    }

    if (e->event->type == MRP_PROC_EVENT_UID) {
        if (!strcmp(name, "ruid"))
            lua_pushinteger(L, e->event->raw->event_data.id.r.ruid);
        else if (!strcmp(name, "euid"))
            lua_pushinteger(L, e->event->raw->event_data.id.e.euid);
        else
            lua_pushnil(L);
        return 1;
    }

    if (e->event->type == MRP_PROC_EVENT_GID) {
        if (!strcmp(name, "rgid"))
            lua_pushinteger(L, e->event->raw->event_data.id.r.rgid);
        else if (!strcmp(name, "egid"))
            lua_pushinteger(L, e->event->raw->event_data.id.e.egid);
        else
            lua_pushnil(L);
        return 1;
    }

    if (e->event->type == MRP_PROC_EVENT_PTRACE) {
        if (!strcmp(name, "tracer_pid"))
            lua_pushinteger(L, e->event->raw->event_data.ptrace.tracer_pid);
        else if (!strcmp(name, "tracer_tgid"))
            lua_pushinteger(L, e->event->raw->event_data.ptrace.tracer_tgid);
        else
            lua_pushnil(L);
        return 1;
    }

    if (e->event->type == MRP_PROC_EVENT_COMM) {
        if (!strcmp(name, "comm"))
            lua_pushstring(L, e->event->raw->event_data.comm.comm);
        else
            lua_pushnil(L);
        return 1;
    }

    if (e->event->type == MRP_PROC_EVENT_EXIT) {
        if (!strcmp(name, "exit_code") || !strcmp(name, "code"))
            lua_pushinteger(L, e->event->raw->event_data.exit.exit_code);
        else if (!strcmp(name, "exit_signal") || !strcmp(name, "signal"))
            lua_pushinteger(L, e->event->raw->event_data.exit.exit_signal);
        else
            lua_pushnil(L);
        return 1;
    }

    lua_pushnil(L);
    return 0;
}


static void event_destroy(void *data)
{
    process_event_lua_t *e = (process_event_lua_t *)data;

    mrp_free(e->event);
    e->event = NULL;
}

static process_event_lua_t *event_check(lua_State *L, int idx)
{
    return (process_event_lua_t *)
        mrp_lua_check_object(L, PROCESS_EVENT_CLASS, idx);
}
