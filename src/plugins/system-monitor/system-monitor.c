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

#include <errno.h>
#include <string.h>

#include <murphy/common/macros.h>
#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/list.h>
#include <murphy/common/mainloop.h>
#include <murphy/core/lua-bindings/murphy.h>
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>

#include "system-monitor.h"
#include "cpu-watch.h"
#include "mem-watch.h"

/*
 * system monitor context (singleton) object
 */

#define SYSMON_LUA_CLASS MRP_LUA_CLASS(sysmon, lua)
#define OFFSET(m)        MRP_OFFSET(sysmon_lua_t, m)
#define NOTIFY           MRP_LUA_CLASS_NOTIFY

struct sysmon_lua_s {
    lua_State       *L;                  /* Lua execution context */
    mrp_list_hook_t  cpu_watches;        /* active CPU watches */
    mrp_list_hook_t  mem_watches;        /* active memory watches */
    int              polling;            /* polling interval (in msecs) */
    mrp_context_t   *ctx;                /* murphy context */
    mrp_timer_t     *t;                  /* polling timer */
    void            *pending;            /* pending notification */
};

static int sysmon_create(lua_State *L);
static void sysmon_destroy(void *data);
static void sysmon_notify(void *data, lua_State *L, int member);
static ssize_t sysmon_tostring(mrp_lua_tostr_mode_t mode, char *buf, size_t size,
                               lua_State *L, void *data);

static int sysmon_add_cpu_watch(lua_State *L);
static int sysmon_add_mem_watch(lua_State *L);

MRP_LUA_METHOD_LIST_TABLE(sysmon_methods,
    MRP_LUA_METHOD_CONSTRUCTOR(sysmon_create)
    MRP_LUA_METHOD(CpuWatch, sysmon_add_cpu_watch)
    MRP_LUA_METHOD(MemWatch, sysmon_add_mem_watch));

MRP_LUA_METHOD_LIST_TABLE(sysmon_overrides,
    MRP_LUA_OVERRIDE_CALL(sysmon_create));

MRP_LUA_MEMBER_LIST_TABLE(sysmon_members,
    MRP_LUA_CLASS_INTEGER("polling", OFFSET(polling), NULL, NULL, NOTIFY));

MRP_LUA_DEFINE_CLASS(sysmon, lua, sysmon_lua_t, sysmon_destroy,
    sysmon_methods, sysmon_overrides, sysmon_members, NULL,
    sysmon_notify , sysmon_tostring , NULL,
                     MRP_LUA_CLASS_EXTENSIBLE | MRP_LUA_CLASS_PRIVREFS);

MRP_LUA_CLASS_CHECKER(sysmon_lua_t, sysmon_lua, SYSMON_LUA_CLASS);

typedef enum {
    SYSMON_MEMBER_POLLING,               /* polling interval member */
} sysmon_member_t;


static sysmon_lua_t *singleton(lua_State *L)
{
    static sysmon_lua_t *sm = NULL;

    if (L != NULL) {
        if (sm == NULL) {
            mrp_debug("creating system monitor singleton object");
            sm = (sysmon_lua_t *)
                mrp_lua_create_object(L, SYSMON_LUA_CLASS, NULL, 0);

            mrp_list_init(&sm->cpu_watches);
            mrp_list_init(&sm->mem_watches);
            sm->L       = L;
            sm->polling = SYSMON_DEFAULT_POLLING;
            sm->ctx     = mrp_lua_get_murphy_context();
        }
    }
    else {
        mrp_debug("clearing system monitor singleton object");
        sm = NULL;
    }

    return sm;
}


static int sysmon_create(lua_State *L)
{
    return mrp_lua_push_object(L, singleton(L));
}


static int sysmon_get(lua_State *L)
{
    return sysmon_create(L);
}


static void sysmon_destroy(void *data)
{
    sysmon_lua_t    *sm = (sysmon_lua_t *)data;
    mrp_list_hook_t *p, *n;

    mrp_del_timer(sm->t);
    sm->t = NULL;

    mrp_list_foreach(&sm->cpu_watches, p, n) {
        mrp_list_delete(p);
    }

    mrp_list_foreach(&sm->mem_watches, p, n) {
        mrp_list_delete(p);
    }

    singleton(NULL);
}


static ssize_t sysmon_tostring(mrp_lua_tostr_mode_t mode, char *buf,
                               size_t size, lua_State *L, void *data)
{
    sysmon_lua_t *sm = (sysmon_lua_t *)data;

    MRP_UNUSED(L);

    switch (mode & MRP_LUA_TOSTR_MODEMASK) {
    case MRP_LUA_TOSTR_LUA:
    default:
        return snprintf(buf, size, "{%s system-monitor @ %d msecs}",
                        sm->t ? "active" : "inactive", sm->polling);
    }
}


static void update_polling(sysmon_lua_t *sm)
{
    mrp_list_hook_t *p, *n;
    cpu_watch_lua_t *cw;
    mem_watch_lua_t *mw;

    mrp_list_foreach(&sm->cpu_watches, p, n) {
        cw = mrp_list_entry(p, typeof(*cw), hook);

        cpu_watch_set_polling(cw, sm->polling);
    }

    mrp_list_foreach(&sm->mem_watches, p, n) {
        mw = mrp_list_entry(p, typeof(*mw), hook);

        mem_watch_set_polling(mw, sm->polling);
    }
}


static void sysmon_notify(void *data, lua_State *L, int member)
{
    sysmon_lua_t *sm = (sysmon_lua_t *)data;

    MRP_UNUSED(L);

    mrp_debug("system monitor member #%d changed", member);

    switch (member) {
    case SYSMON_MEMBER_POLLING:
        if (sm->polling < SYSMON_MINIMUM_POLLING)
            sm->polling = SYSMON_MINIMUM_POLLING;

        if (sm->t != NULL)
            mrp_mod_timer(sm->t, sm->polling);

        update_polling(sm);

        mrp_log_info("system monitor polling interval set to %d msecs.",
                     sm->polling);
        break;

    default:
        break;
    }
}


static void sysmon_timer(mrp_timer_t *t, void *user_data)
{
    sysmon_lua_t    *sm = (sysmon_lua_t *)user_data;
    mrp_list_hook_t *p, *n;
    cpu_watch_lua_t *cw;
    mem_watch_lua_t *mw;

    MRP_UNUSED(t);

    mrp_debug("sampling CPU load");
    cpu_sample_load();

    mrp_list_foreach(&sm->cpu_watches, p, n) {
        cw = mrp_list_entry(p, typeof(*cw), hook);

        sm->pending = n;

        if (cpu_watch_update(cw, sm->L))
            cpu_watch_notify(cw, sm->L);

        n = sm->pending;
    }

    mrp_debug("sampling memory usage");
    mem_sample_usage();

    mrp_list_foreach(&sm->mem_watches, p, n) {
        mw = mrp_list_entry(p, typeof(*mw), hook);

        sm->pending = n;

        if (mem_watch_update(mw, sm->L))
            mem_watch_notify(mw, sm->L);

        n = sm->pending;
    }

}


static int sysmon_add_cpu_watch(lua_State *L)
{
    sysmon_lua_t    *sm;
    cpu_watch_lua_t *w;

    sm = sysmon_lua_check(L, 1);

    if ((w = cpu_watch_create(sm, sm->polling, L)) == NULL)
        return luaL_error(L, "failed to create CPU watch");

    w->watchref = mrp_lua_object_ref_value(sm, L, -1);
    mrp_list_append(&sm->cpu_watches, &w->hook);

    if (sm->t == NULL)
        sm->t = mrp_add_timer(sm->ctx->ml, sm->polling, sysmon_timer, sm);

    return 1;
}


static int sysmon_add_mem_watch(lua_State *L)
{
    sysmon_lua_t    *sm;
    mem_watch_lua_t *w;

    sm = sysmon_lua_check(L, 1);

    if ((w = mem_watch_create(sm, sm->polling, L)) == NULL)
        return luaL_error(L, "failed to create memory watch");

    w->watchref = mrp_lua_object_ref_value(sm, L, -1);
    mrp_list_append(&sm->mem_watches, &w->hook);

    if (sm->t == NULL)
        sm->t = mrp_add_timer(sm->ctx->ml, sm->polling, sysmon_timer, sm);

    return 1;
}


int sysmon_del_cpu_watch(sysmon_lua_t *sm, cpu_watch_lua_t *w)
{
    if (sm->pending == &w->hook)
        sm->pending = w->hook.next;

    mrp_lua_object_unref_value(sm, sm->L, w->watchref);

    if (mrp_list_empty(&sm->cpu_watches)) {
        mrp_del_timer(sm->t);
        sm->t = NULL;
    }

    return 0;
}


int sysmon_del_mem_watch(sysmon_lua_t *sm, mem_watch_lua_t *w)
{
    if (sm->pending == &w->hook)
        sm->pending = w->hook.next;

    mrp_lua_object_unref_value(sm, sm->L, w->watchref);

    if (mrp_list_empty(&sm->mem_watches)) {
        mrp_del_timer(sm->t);
        sm->t = NULL;
    }

    return 0;
}


MURPHY_REGISTER_LUA_BINDINGS(murphy, SYSMON_LUA_CLASS,
                             { "get_system_monitor", sysmon_get });
