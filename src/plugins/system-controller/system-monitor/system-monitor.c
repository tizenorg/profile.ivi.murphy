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
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/core/lua-bindings/murphy.h>

#include "cpu.h"
#include "memory.h"

#define MIN_INTERVAL       1000          /* minimal sampling interval */
#define DEFAULT_INTERVAL   5000          /* default sampling interval */

typedef struct sysmon_s     sysmon_t;
typedef struct sysmon_lua_s sysmon_lua_t;

static int sysmon_lua_create(lua_State *L);
static void sysmon_lua_destroy(void *data);
static void sysmon_lua_changed(void *data, lua_State *L, int member);
static ssize_t sysmon_lua_tostring(mrp_lua_tostr_mode_t mode, char *buf,
                                   size_t size, lua_State *L, void *data);

static int sysmon_lua_add_cpu_watch(lua_State *L);
static int sysmon_lua_del_cpu_watch(lua_State *L);
static int sysmon_lua_add_mem_watch(lua_State *L);
static int sysmon_lua_del_mem_watch(lua_State *L);


/*
 * Lua system-monitor object
 */

#define SYSMON_LUA_CLASS MRP_LUA_CLASS(sysmon, lua)

struct sysmon_lua_s {
    mrp_context_t   *ctx;                /* murphy context */
    lua_State       *L;                  /* Lua execution context */
    mrp_list_hook_t  cpu_watches;        /* active CPU watches */
    mrp_list_hook_t  mem_watches;        /* active memory watches */
    int              interval;           /* sampling interval */
    mrp_timer_t     *t;                  /* sampling timer */
    int              next_id;            /* next watch id */
};


/*
 * CPU load monitor
 */

typedef struct {
    char *name;                          /* limit name */
    int   limit;                         /* upper limit */
} cpu_limit_t;

typedef enum {
    CPU_WATCH_INVALID,
    CPU_WATCH_LOAD,                      /* track overall CPU load */
    CPU_WATCH_IDLE,                      /* track CPU idle state */
    CPU_WATCH_IOWAIT                     /* track CPU I/O wait state */
} cpu_watch_type_t;

typedef struct {
    mrp_list_hook_t    hook;             /* to list of watches */
    int                id;               /* watch id */
    cpu_watch_type_t   type;             /* watch type, what to track */
    cpu_limit_t       *limits;           /* limits for this watch */
    int                nlimit;           /* number of limits */
    cpu_limit_t       *limit;            /* limit index of last sampled value */
    int                callback;         /* reference to callback */
    int                data;             /* reference to callback data */
} cpu_watch_t;


/*
 * memory usage monitor
 */

typedef struct {
    char     *name;                      /* limit name */
    uint64_t  limit;                     /* upper limit */
} mem_limit_t;

typedef enum {
    MEM_WATCH_INVALID,
    MEM_WATCH_FREEMEM,
    MEM_WATCH_FREESWAP,
    MEM_WATCH_DIRTY,
    MEM_WATCH_WRITEBACK,
} mem_watch_type_t;

typedef struct {
    mrp_list_hook_t    hook;             /* to list of watches */
    int                id;               /* watch id */
    mem_watch_type_t   type;             /* watch type, what to track */
    mem_limit_t       *limits;           /* limits for this watch */
    int                nlimit;           /* number of limits */
    mem_limit_t       *limit;            /* limit index of last sampled value */
    int                callback;         /* reference to callback */
    int                data;             /* reference to callback data */
} mem_watch_t;



/*
 * Lua system-monitor class
 */

#define OFFS(m) MRP_OFFSET(sysmon_lua_t, m)
#define NOTIFY  MRP_LUA_CLASS_NOTIFY

MRP_LUA_METHOD_LIST_TABLE(sysmon_lua_methods,
    MRP_LUA_METHOD_CONSTRUCTOR(sysmon_lua_create)
    MRP_LUA_METHOD(add_cpu_watch, sysmon_lua_add_cpu_watch)
    MRP_LUA_METHOD(del_cpu_watch, sysmon_lua_del_cpu_watch)
    MRP_LUA_METHOD(add_mem_watch, sysmon_lua_add_mem_watch)
    MRP_LUA_METHOD(del_mem_watch, sysmon_lua_del_mem_watch));

MRP_LUA_METHOD_LIST_TABLE(sysmon_lua_overrides,
    MRP_LUA_OVERRIDE_CALL(sysmon_lua_create));

MRP_LUA_MEMBER_LIST_TABLE(sysmon_lua_members,
    MRP_LUA_CLASS_INTEGER("interval", OFFS(interval), NULL, NULL, NOTIFY));

MRP_LUA_DEFINE_CLASS(sysmon, lua, sysmon_lua_t, sysmon_lua_destroy,
                     sysmon_lua_methods, sysmon_lua_overrides,
                     sysmon_lua_members, NULL, sysmon_lua_changed,
                     sysmon_lua_tostring, NULL, MRP_LUA_CLASS_EXTENSIBLE);

MRP_LUA_CLASS_CHECKER(sysmon_lua_t, sysmon_lua, SYSMON_LUA_CLASS);

typedef enum {
    SYSMON_MEMBER_INTERVAL,
} sysmon_member_t;


static inline cpu_watch_type_t cpu_watch_type(const char *name)
{
    if      (!strcmp(name, "load"))   return CPU_WATCH_LOAD;
    else if (!strcmp(name, "idle"))   return CPU_WATCH_IDLE;
    else if (!strcmp(name, "iowait")) return CPU_WATCH_IOWAIT;
    else                              return CPU_WATCH_INVALID;
}


static inline const char *cpu_watch_name(cpu_watch_type_t type)
{
    switch (type) {
    case CPU_WATCH_LOAD:   return "load";
    case CPU_WATCH_IDLE:   return "idle";
    case CPU_WATCH_IOWAIT: return "iowait";
    default:               return "<invalid>";
    }
}


static int cmp_cpulimits(const void *ptr1, const void *ptr2)
{
    cpu_limit_t *l1 = (cpu_limit_t *)ptr1;
    cpu_limit_t *l2 = (cpu_limit_t *)ptr2;

    return l1->limit - l2->limit;
}


static cpu_watch_t *cpu_watch_create(sysmon_lua_t *sm, char *ebuf, size_t esize)
{
    lua_State   *L = sm->L;
    cpu_watch_t *w;
    const char  *kname;
    int          ktype, limit, i;
    size_t       klen;
    bool         has_max;

    if ((w = mrp_allocz(sizeof(*w))) == NULL) {
        snprintf(ebuf, esize, "faled to allocate CPU watch");
        return NULL;
    }

    mrp_list_init(&w->hook);
    w->callback = LUA_NOREF;
    w->data     = LUA_NOREF;

    if ((w->type = cpu_watch_type(lua_tostring(L, 2))) == CPU_WATCH_INVALID) {
        snprintf(ebuf, esize, "invalid CPU watch type %s", lua_tostring(L, 2));
        goto fail;
    }

    has_max = false;

    MRP_LUA_FOREACH_ALL(L, i, 3, ktype, kname, klen) {
        if (ktype != LUA_TSTRING || lua_type(L, -1) != LUA_TNUMBER) {
            snprintf(ebuf, esize, "invalid CPU watch limit entry #%d", i);
            goto fail;
        }

        if ((limit = lua_tointeger(L, -1)) < 0 || limit > 100) {
            snprintf(ebuf, esize, "CPU watch limit #%d (%d %%) out of range",
                     i, limit);
            goto fail;
        }

        if (mrp_reallocz(w->limits, w->nlimit, w->nlimit + 1) == NULL) {
            snprintf(ebuf, esize, "failed to extend CPU watch limit table");
            goto fail;
        }

        if ((w->limits[i].name = mrp_strdup(kname)) == NULL) {
            snprintf(ebuf, esize, "failed to extend CPU watch limit table");
            goto fail;
        }

        w->limits[i].limit = limit;
        w->nlimit++;

        if (limit == 100)
            has_max = true;
    }

    if (w->nlimit < 1) {
        snprintf(ebuf, esize, "expecting at least 1 CPU watch limit");
        goto fail;
    }

    if (!has_max) {
        if (mrp_reallocz(w->limits, w->nlimit, w->nlimit + 1) == NULL) {
            snprintf(ebuf, esize, "failed to add CPU watch limit 100");
            goto fail;
        }

        if ((w->limits[w->nlimit].name = mrp_strdup("full")) == NULL) {
            snprintf(ebuf, esize, "failed to add CPU watch limit 100");
            goto fail;
        }

        w->limits[w->nlimit].limit = 100;
        w->nlimit++;
    }

    qsort(w->limits, w->nlimit, sizeof(w->limits[0]), cmp_cpulimits);
    w->limit = w->limits;

    w->id       = sm->next_id++;
    w->callback = mrp_lua_object_ref_value(sm, L, 4);
    w->data     = mrp_lua_object_ref_value(sm, L, 5);

    mrp_list_append(&sm->cpu_watches, &w->hook);

    return w;

 fail:
    for (i = 0; i < w->nlimit; i++)
        mrp_free(w->limits[i].name);
    mrp_free(w->limits);
    mrp_free(w);

    return NULL;
}


static void cpu_watch_destroy(sysmon_lua_t *sm, cpu_watch_t *w)
{
    int i;

    mrp_list_delete(&w->hook);
    mrp_lua_object_unref_value(sm, sm->L, w->callback);
    mrp_lua_object_unref_value(sm, sm->L, w->data);

    for (i = 0; i < w->nlimit; i++)
        mrp_free(w->limits[i].name);
    mrp_free(w->limits);

    mrp_free(w);
}


static cpu_watch_t *cpu_watch_find(sysmon_lua_t *sm, int id)
{
    mrp_list_hook_t *p, *n;
    cpu_watch_t     *w;

    mrp_list_foreach(&sm->cpu_watches, p, n) {
        w = mrp_list_entry(p, typeof(*w), hook);

        if (w->id == id)
            return w;
    }

    return NULL;
}


static void cpu_watch_notify(sysmon_lua_t *sm, cpu_watch_t *w, const char *prev,
                             const char *curr)
{
    if (!mrp_lua_object_deref_value(sm, sm->L, w->callback, false)) {
        mrp_log_error("Failed to dereference Lua CPU watch callback.");
        return;
    }

    lua_pushstring(sm->L, cpu_watch_name(w->type));
    lua_pushstring(sm->L, prev);
    lua_pushstring(sm->L, curr);
    mrp_lua_object_deref_value(sm, sm->L, w->data, true);

    if (lua_pcall(sm->L, 4, 0, 0) != 0)
        mrp_log_error("Failed to notify Lua CPU watch (error: %s).",
                      lua_tostring(sm->L, -1));
}


static void cpu_watch_check(sysmon_lua_t *sm, cpu_watch_t *w,
                            int load, int idle, int iowait)
{
    cpu_limit_t *l;
    int          i, value;

    switch (w->type) {
    case CPU_WATCH_LOAD:   value = load;   break;
    case CPU_WATCH_IDLE:   value = idle;   break;
    case CPU_WATCH_IOWAIT: value = iowait; break;
    default:                               return;
    }

    for (i = 0, l = w->limits; i < w->nlimit; i++, l++) {
        if (value <= l->limit) {
            if (w->limit != l) {
                cpu_watch_notify(sm, w, w->limit->name, l->name);
                w->limit = l;
            }
            break;
        }
    }
}


static inline mem_watch_type_t mem_watch_type(const char *name)
{
    if      (!strcasecmp(name, "MemFree"))   return MEM_WATCH_FREEMEM;
    else if (!strcasecmp(name, "SwapFree"))  return MEM_WATCH_FREESWAP;
    else if (!strcasecmp(name, "Dirty"))     return MEM_WATCH_DIRTY;
    else if (!strcasecmp(name, "Writeback")) return MEM_WATCH_WRITEBACK;
    else                                     return MEM_WATCH_INVALID;
}


static inline const char *mem_watch_name(mem_watch_type_t type)
{
    switch (type) {
    case MEM_WATCH_FREEMEM:   return "MemFree";
    case MEM_WATCH_FREESWAP:  return "SwapFree";
    case MEM_WATCH_DIRTY:     return "Dirty";
    case MEM_WATCH_WRITEBACK: return "Writeback";
    default:                  return "<invalid>";
    }
}


static int cmp_memlimits(const void *ptr1, const void *ptr2)
{
    mem_limit_t *l1 = (mem_limit_t *)ptr1;
    mem_limit_t *l2 = (mem_limit_t *)ptr2;

    if (l1->limit < l2->limit)
        return -1;
    else if (l1->limit > l2->limit)
        return 1;
    else
        return 0;
}


static mem_watch_t *mem_watch_create(sysmon_lua_t *sm, char *ebuf, size_t esize)
{
    lua_State   *L = sm->L;
    mem_watch_t *w;
    const char  *kname, *strlim;
    char        *e;
    uint64_t     limit;
    int          ktype, i;
    size_t       klen;

    if ((w = mrp_allocz(sizeof(*w))) == NULL) {
        snprintf(ebuf, esize, "faled to allocate CPU watch");
        return NULL;
    }

    mrp_list_init(&w->hook);
    w->callback = LUA_NOREF;
    w->data     = LUA_NOREF;

    if ((w->type = mem_watch_type(lua_tostring(L, 2))) == MEM_WATCH_INVALID) {
        snprintf(ebuf, esize, "invalid memory watch type %s",
                 lua_tostring(L, 2));
        goto fail;
    }

    MRP_LUA_FOREACH_ALL(L, i, 3, ktype, kname, klen) {
        if (ktype != LUA_TSTRING) {
            snprintf(ebuf, esize, "invalid memory watch limit entry #%d", i);
            goto fail;
        }

        switch (lua_type(L, -1)) {
        case LUA_TNUMBER:
            limit = (uint64_t)lua_tonumber(L, -1);
            break;

        case LUA_TSTRING:
            strlim = lua_tostring(L, -1);
            limit  = strtoull(strlim, &e, 10);
            if (e != NULL) {
                if (e[0] && !e[1]) {
                    switch (e[0]) {
                    case 'k': limit *= 1024;               break;
                    case 'M': limit *= 1024 * 1024;        break;
                    case 'G': limit *= 1024 * 1024 * 1024; break;
                    default:
                        snprintf(ebuf, esize,
                                 "invalid memory limit suffix in '%s'", strlim);
                        goto fail;
                    }
                }
                else {
                    snprintf(ebuf, esize, "invalid memory limit '%s'", strlim);
                    goto fail;
                }
            }
            break;

        default:
            snprintf(ebuf, esize, "invalid memory limit entry #%d", i);
            goto fail;
        }

        if (mrp_reallocz(w->limits, w->nlimit, w->nlimit + 1) == NULL) {
            snprintf(ebuf, esize, "failed to extend memory watch limit table");
            goto fail;
        }

        if ((w->limits[i].name = mrp_strdup(kname)) == NULL) {
            snprintf(ebuf, esize, "failed to extend memory watch limit table");
            goto fail;
        }

        w->limits[i].limit = limit;
        w->nlimit++;
    }

    if (w->nlimit < 1) {
        snprintf(ebuf, esize, "expecting at least 1 memory watch limit");
        goto fail;
    }

    qsort(w->limits, w->nlimit, sizeof(w->limits[0]), cmp_memlimits);
    w->limit = w->limits;

    w->id       = sm->next_id++;
    w->callback = mrp_lua_object_ref_value(sm, L, 4);
    w->data     = mrp_lua_object_ref_value(sm, L, 5);

    mrp_list_append(&sm->mem_watches, &w->hook);

    return w;

 fail:
    for (i = 0; i < w->nlimit; i++)
        mrp_free(w->limits[i].name);
    mrp_free(w->limits);
    mrp_free(w);

    return NULL;
}


static void mem_watch_destroy(sysmon_lua_t *sm, mem_watch_t *w)
{
    int i;

    mrp_list_delete(&w->hook);
    mrp_lua_object_unref_value(sm, sm->L, w->callback);
    mrp_lua_object_unref_value(sm, sm->L, w->data);

    for (i = 0; i < w->nlimit; i++)
        mrp_free(w->limits[i].name);
    mrp_free(w->limits);

    mrp_free(w);
}


static mem_watch_t *mem_watch_find(sysmon_lua_t *sm, int id)
{
    mrp_list_hook_t *p, *n;
    mem_watch_t     *w;

    mrp_list_foreach(&sm->mem_watches, p, n) {
        w = mrp_list_entry(p, typeof(*w), hook);

        if (w->id == id)
            return w;
    }

    return NULL;
}


static void mem_watch_notify(sysmon_lua_t *sm, mem_watch_t *w, const char *prev,
                             const char *curr)
{
    if (!mrp_lua_object_deref_value(sm, sm->L, w->callback, false)) {
        mrp_log_error("Failed to dereference Lua memory watch callback.");
        return;
    }

    lua_pushstring(sm->L, mem_watch_name(w->type));
    lua_pushstring(sm->L, prev);
    lua_pushstring(sm->L, curr);
    mrp_lua_object_deref_value(sm, sm->L, w->data, true);

    if (lua_pcall(sm->L, 4, 0, 0) != 0)
        mrp_log_error("Failed to notify Lua memory watch (error: %s).",
                      lua_tostring(sm->L, -1));
}


static void mem_watch_check(sysmon_lua_t *sm, mem_watch_t *w, mem_usage_t *m)
{
    mem_limit_t *l;
    uint64_t     value;
    int          i;

    switch (w->type) {
    case MEM_WATCH_FREEMEM:   value = m->mem_free;  break;
    case MEM_WATCH_FREESWAP:  value = m->swap_free; break;
    case MEM_WATCH_DIRTY:     value = m->dirty;     break;
    case MEM_WATCH_WRITEBACK: value = m->writeback; break;
    default:                                        return;
    }

    for (i = 0, l = w->limits; i < w->nlimit; i++, l++) {
        if (value <= l->limit) {
            if (w->limit != l) {
                mem_watch_notify(sm, w, w->limit->name, l->name);
                w->limit = l;
            }
            break;
        }
    }
}


static void sample_load(mrp_timer_t *t, void *user_data)
{
    sysmon_lua_t    *sm = (sysmon_lua_t *)user_data;
    int              idle, iowait, load;
    mrp_list_hook_t *p, *n;
    cpu_watch_t     *cw;
    mem_watch_t     *mw;
    mem_usage_t      mem;

    MRP_UNUSED(t);

    if (cpu_get_load(&load, &idle, &iowait) < 0) {
        if (errno != EAGAIN)
            mrp_log_error("Failed to get CPU load (%d: %s).", errno,
                          strerror(errno));
    }
    else {
        mrp_debug("load = %d, idle = %d, iowait = %d", load, idle, iowait);

        mrp_list_foreach(&sm->cpu_watches, p, n) {
            cw = mrp_list_entry(p, typeof(*cw), hook);
            cpu_watch_check(sm, cw, load, idle, iowait);
        }
    }

    if (mem_get_usage(&mem) == 0) {
        mrp_debug("MemFree = %llu, SwapFree = %llu",
                  mem.mem_free, mem.swap_free);
        mrp_debug("Dirty = %llu, Writeback = %llu",
                  mem.dirty, mem.writeback);

        mrp_list_foreach(&sm->mem_watches, p, n) {
            mw = mrp_list_entry(p, typeof(*mw), hook);
            mem_watch_check(sm, mw, &mem);
        }
    }
    else
        mrp_log_error("Failed to get memory usage (%d: %s).", errno,
                      strerror(errno));
}


static void sysmon_lua_changed(void *data, lua_State *L, int member)
{
    sysmon_lua_t *sm = (sysmon_lua_t *)data;

    MRP_UNUSED(L);

    switch (member) {
    case SYSMON_MEMBER_INTERVAL:
        if (sm->interval < MIN_INTERVAL)
            sm->interval = MIN_INTERVAL;

        if (sm->t != NULL)
            mrp_mod_timer(sm->t, sm->interval);

        mrp_log_info("system-monitor: sampling interval set to %d msecs.",
                     sm->interval);
        break;

    default:
        break;
    }
}


static sysmon_lua_t *singleton(lua_State *L)
{
    static sysmon_lua_t *sm = NULL;

    if (L == NULL) {
        mrp_debug("clearing system-monitor singleton object");
        sm = NULL;
    }
    else if (sm == NULL) {
        mrp_debug("creating system-monitor singleton object");
        sm = (sysmon_lua_t *)mrp_lua_create_object(L, SYSMON_LUA_CLASS,
                                                   NULL, 0);
        mrp_list_init(&sm->cpu_watches);
        mrp_list_init(&sm->mem_watches);

        sm->L        = L;
        sm->ctx      = mrp_lua_get_murphy_context();
        sm->next_id  = 1;
        sm->interval = DEFAULT_INTERVAL;
    }

    return sm;
}


static int sysmon_lua_create(lua_State *L)
{
    sysmon_lua_t *sm = singleton(L);

    mrp_lua_push_object(L, sm);

    return 1;
}


static int sysmon_lua_get(lua_State *L)
{
    return sysmon_lua_create(L);
}


static void sysmon_lua_destroy(void *data)
{
    sysmon_lua_t    *sm = (sysmon_lua_t *)data;
    mrp_list_hook_t *p, *n;
    cpu_watch_t     *w;

    mrp_list_foreach(&sm->cpu_watches, p, n) {
        w = mrp_list_entry(p, typeof(*w), hook);
        cpu_watch_destroy(sm, w);
    }

    mrp_del_timer(sm->t);
    sm->t = NULL;

    singleton(NULL);
}


static int sysmon_lua_add_cpu_watch(lua_State *L)
{
    sysmon_lua_t *sm;
    cpu_watch_t  *w;
    char          err[256];
    int           narg;

    if ((narg = lua_gettop(L)) != 5)
        return luaL_error(L, "expecting 5 arguments, got %d", narg);

    sm = sysmon_lua_check(L, 1);

    luaL_checktype(L, 2, LUA_TSTRING);
    luaL_checktype(L, 3, LUA_TTABLE);
    luaL_checktype(L, 4, LUA_TFUNCTION);
    luaL_checkany(L, 5);

    if ((w = cpu_watch_create(sm, err, sizeof(err))) == NULL)
        return luaL_error(L, "%s", err);

    if (sm->t == NULL)
        sm->t = mrp_add_timer(sm->ctx->ml, sm->interval, sample_load, sm);

    lua_pushinteger(L, w->id);
    return 1;
}


static int sysmon_lua_del_cpu_watch(lua_State *L)
{
    sysmon_lua_t *sm = sysmon_lua_check(L, 1);
    int           id = luaL_checkinteger(L, 2);
    cpu_watch_t  *w  = cpu_watch_find(sm, id);

    if (w != NULL) {
        cpu_watch_destroy(sm, w);

        if (mrp_list_empty(&sm->cpu_watches) &&
            mrp_list_empty(&sm->mem_watches)) {
            mrp_del_timer(sm->t);
            sm->t = NULL;
        }
    }

    return 0;
}


static int sysmon_lua_add_mem_watch(lua_State *L)
{
    sysmon_lua_t *sm;
    mem_watch_t  *w;
    char          err[256];
    int           narg;

    if ((narg = lua_gettop(L)) != 5)
        return luaL_error(L, "expecting 5 arguments, got %d", narg);

    sm = sysmon_lua_check(L, 1);

    luaL_checktype(L, 2, LUA_TSTRING);
    luaL_checktype(L, 3, LUA_TTABLE);
    luaL_checktype(L, 4, LUA_TFUNCTION);
    luaL_checkany(L, 5);

    if ((w = mem_watch_create(sm, err, sizeof(err))) == NULL)
        return luaL_error(L, "%s", err);

    if (sm->t == NULL)
        sm->t = mrp_add_timer(sm->ctx->ml, sm->interval, sample_load, sm);

    lua_pushinteger(L, w->id);
    return 1;
}


static int sysmon_lua_del_mem_watch(lua_State *L)
{
    sysmon_lua_t *sm = sysmon_lua_check(L, 1);
    int           id = luaL_checkinteger(L, 2);
    mem_watch_t  *w  = mem_watch_find(sm, id);

    if (w != NULL) {
        mem_watch_destroy(sm, w);

        if (mrp_list_empty(&sm->cpu_watches) &&
            mrp_list_empty(&sm->mem_watches)) {
            mrp_del_timer(sm->t);
            sm->t = NULL;
        }
    }

    return 0;
}


static ssize_t sysmon_lua_tostring(mrp_lua_tostr_mode_t mode, char *buf,
                                   size_t size, lua_State *L, void *data)
{
    sysmon_lua_t *sm = (sysmon_lua_t *)data;

    MRP_UNUSED(L);

    switch (mode & MRP_LUA_TOSTR_MODEMASK) {
    case MRP_LUA_TOSTR_LUA:
    default:
        return snprintf(buf, size, "{%s system-monitor @ %d msecs}",
                        sm->t ? "active" : "inactive", sm->interval);
    }
}




MURPHY_REGISTER_LUA_BINDINGS(murphy, SYSMON_LUA_CLASS,
                             { "get_system_monitor", sysmon_lua_get });
