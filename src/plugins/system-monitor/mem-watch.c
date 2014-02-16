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
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/core/lua-bindings/murphy.h>

#include "mem-watch.h"

/*
 * memory watch object
 */

#define MEM_WATCH_LUA_CLASS MRP_LUA_CLASS(mem_watch, lua)
#define RO                  MRP_LUA_CLASS_READONLY
#define NOINIT              MRP_LUA_CLASS_NOINIT
#define NOFLAGS             MRP_LUA_CLASS_NOFLAGS
#define SETANDGET           mem_watch_setmember, mem_watch_getmember
#define setmember           mem_watch_setmember
#define getmember           mem_watch_getmember

static int mem_watch_no_constructor(lua_State *L);
static void mem_watch_destroy(void *data);
static void mem_watch_changed(void *data, lua_State *L, int member);
static ssize_t mem_watch_tostring(mrp_lua_tostr_mode_t mode, char *buf,
                                  size_t size, lua_State *L, void *data);
static int mem_watch_setmember(void *data, lua_State *L, int member,
                               mrp_lua_value_t *v);
static int mem_watch_getmember(void *data, lua_State *L, int member,
                               mrp_lua_value_t *v);
static int mem_watch_delete(lua_State *L);

MRP_LUA_METHOD_LIST_TABLE(mem_watch_methods,
    MRP_LUA_METHOD_CONSTRUCTOR(mem_watch_no_constructor)
    MRP_LUA_METHOD(delete, mem_watch_delete));

MRP_LUA_METHOD_LIST_TABLE(mem_watch_overrides,
    MRP_LUA_OVERRIDE_CALL(mem_watch_no_constructor));

MRP_LUA_MEMBER_LIST_TABLE(mem_watch_members,
    MRP_LUA_CLASS_STRING ("sample"  , 0, setmember, getmember, RO        )
    MRP_LUA_CLASS_ANY    ("limits"  , 0, setmember, getmember, RO        )
    MRP_LUA_CLASS_INTEGER("polling" , 0, setmember, getmember, RO|NOINIT )
    MRP_LUA_CLASS_INTEGER("alpha"   , 0, setmember, getmember,    NOFLAGS)
    MRP_LUA_CLASS_INTEGER("window"  , 0, setmember, getmember,    NOFLAGS)
    MRP_LUA_CLASS_INTEGER("raw"     , 0, setmember, getmember, RO|NOINIT )
    MRP_LUA_CLASS_INTEGER("value"   , 0, setmember, getmember, RO|NOINIT )
    MRP_LUA_CLASS_STRING ("current" , 0, setmember, getmember, RO|NOINIT )
    MRP_LUA_CLASS_STRING ("previous", 0, setmember, getmember, RO|NOINIT )
    MRP_LUA_CLASS_ANY    ("notify"  , 0, setmember, getmember,    NOFLAGS)
    MRP_LUA_CLASS_ANY    ("update"  , 0, setmember, getmember,    NOFLAGS));

MRP_LUA_DEFINE_CLASS(mem_watch, lua, mem_watch_lua_t, mem_watch_destroy,
    mem_watch_methods, mem_watch_overrides, mem_watch_members, NULL,
    mem_watch_changed, mem_watch_tostring , NULL,
                     MRP_LUA_CLASS_EXTENSIBLE | MRP_LUA_CLASS_PRIVREFS);

MRP_LUA_CLASS_CHECKER(mem_watch_lua_t, mem_watch_lua, MEM_WATCH_LUA_CLASS);

typedef enum {
    MEM_WATCH_MEMBER_SAMPLE,
    MEM_WATCH_MEMBER_LIMITS,
    MEM_WATCH_MEMBER_POLLING,
    MEM_WATCH_MEMBER_WINDOW,
    MEM_WATCH_MEMBER_ALPHA,
    MEM_WATCH_MEMBER_RAW,
    MEM_WATCH_MEMBER_VALUE,
    MEM_WATCH_MEMBER_CURRENT,
    MEM_WATCH_MEMBER_PREVIOUS,
    MEM_WATCH_MEMBER_NOTIFY,
    MEM_WATCH_MEMBER_UPDATE
} mem_watch_member_t;

static inline mem_sample_t get_sample_id(const char *name);
static inline const char *get_sample_name(mem_sample_t id);
static inline void recalc_smoothing(mem_watch_lua_t *w);
static int setup_limits(mem_watch_lua_t *w, lua_State *L, int limref);
static void cleanup_limits(mem_watch_lua_t *w, lua_State *L,
                           mem_limit_t *limits, int n, int limref);


static int mem_watch_no_constructor(lua_State *L)
{
    return luaL_error(L, "trying create a memory watch via constructor.");
}


mem_watch_lua_t *mem_watch_create(sysmon_lua_t *sm, int polling, lua_State *L)
{
    mem_watch_lua_t *w;
    char             e[256];

    luaL_checktype(L, 2, LUA_TTABLE);

    w = (mem_watch_lua_t *)mrp_lua_create_object(L, MEM_WATCH_LUA_CLASS,
                                                 NULL, 0);

    mrp_list_init(&w->hook);
    w->sysmon  = sm;
    w->polling = polling;
    w->limref  = LUA_NOREF;
    w->window  = -1;
    w->value.a = -1;

    if (mrp_lua_init_members(w, L, 2, e, sizeof(e)) != 1) {
        luaL_error(L, "failed to initialize memory watch (error: %s)",
                   *e ? e : "<unknown error>");
        return NULL;
    }

    if (w->window == -1 && w->value.a == -1)
        w->window = MEM_WATCH_WINDOW;

    if (w->window != -1)
        recalc_smoothing(w);

    return w;
}


static void mem_watch_destroy(void *data)
{
    MRP_UNUSED(data);

    mrp_debug("memory watch %p destroyed", data);

    return;
}


static int mem_watch_delete(lua_State *L)
{
    mem_watch_lua_t *w = mem_watch_lua_check(L, 1);

    mrp_list_delete(&w->hook);
    mrp_list_init(&w->hook);

    sysmon_del_mem_watch(w->sysmon, w);

    cleanup_limits(w, L, w->limits, -1, w->limref);
    w->limits = NULL;
    w->limref = LUA_NOREF;

    mrp_funcbridge_unref(L, w->update);
    mrp_funcbridge_unref(L, w->notify);
    w->update = NULL;
    w->notify = NULL;

    return 0;
}


static void mem_watch_changed(void *data, lua_State *L, int member)
{
    MRP_UNUSED(data);
    MRP_UNUSED(L);
    MRP_UNUSED(member);
}


static int mem_watch_setmember(void *data, lua_State *L, int member,
                               mrp_lua_value_t *v)
{
    mem_watch_lua_t  *w = (mem_watch_lua_t *)data;
    mrp_funcbridge_t *f, **fptr;

    switch (member) {
    case MEM_WATCH_MEMBER_SAMPLE:
        if ((w->sample = get_sample_id(v->str)) >= 0)
            return 1;
        else
            mrp_log_error("Can't sample memory for unknown type '%s'.", v->str);
        return 0;

    case MEM_WATCH_MEMBER_WINDOW:
        mem_watch_set_window(w, v->s32);
        return 1;

    case MEM_WATCH_MEMBER_ALPHA:
        if (w->window == -1) {
            w->value.a = v->dbl;
            return 1;
        }
        else {
            mrp_log_error("Can't set both window and alpha for memory watch.");
            return 0;
        }
        break;

    case MEM_WATCH_MEMBER_NOTIFY: fptr = &w->notify; goto set_bridge;
    case MEM_WATCH_MEMBER_UPDATE: fptr = &w->update;
    set_bridge:
        if (!mrp_lua_object_deref_value(w, L, v->any, false))
            return 0;
        switch (lua_type(L, -1)) {
        case LUA_TFUNCTION:
            f = *fptr = mrp_funcbridge_create_luafunc(L, -1);
            break;
        default:
            f = NULL;
            break;
        }
        lua_pop(L, 1);
        mrp_lua_object_unref_value(w, L, v->any);

        return (f != NULL ? 1 : 0);

    case MEM_WATCH_MEMBER_LIMITS:
        return setup_limits(w, L, v->any);

    default:
        mrp_log_error("Trying to set read-only memory watch member #%d.",
                      member);
        return 0;
    }
}


static int mem_watch_getmember(void *data, lua_State *L, int member,
                               mrp_lua_value_t *v)
{
    mem_watch_lua_t *w  = (mem_watch_lua_t *)data;

    MRP_UNUSED(L);

    switch (member) {
    case MEM_WATCH_MEMBER_SAMPLE:
        v->str = get_sample_name(w->sample);
        return 1;

    case MEM_WATCH_MEMBER_LIMITS:
        v->any = w->limref;
        return 1;

    case MEM_WATCH_MEMBER_WINDOW:
        v->s32 = w->window;
        return 1;

    case MEM_WATCH_MEMBER_ALPHA:
        v->dbl = w->value.a;
        return 1;

    case MEM_WATCH_MEMBER_POLLING:
        v->s32 = w->polling;
        return 1;

    case MEM_WATCH_MEMBER_VALUE:
        v->s32 = w->value.S;
        return 1;

    case MEM_WATCH_MEMBER_RAW:
        v->s32 = w->value.sample;
        return 1;

    case MEM_WATCH_MEMBER_CURRENT:
        v->str = w->curr ? w->curr->label : "<unknown limit>";
        return 1;

    case MEM_WATCH_MEMBER_PREVIOUS:
        v->str = w->prev ? w->prev->label : "<unknown limit>";
        return 1;

    default:
        v->any = LUA_REFNIL;
        return 1;
    }
}


static ssize_t mem_watch_tostring(mrp_lua_tostr_mode_t mode, char *buf,
                                  size_t size, lua_State *L, void *data)
{
    mem_watch_lua_t *w = (mem_watch_lua_t *)data;

    MRP_UNUSED(L);

    switch (mode & MRP_LUA_TOSTR_MODEMASK) {
    case MRP_LUA_TOSTR_LUA:
    default:
        return snprintf(buf, size,
                        "{%s memory watch, %d/%d sec window/poll, %.2f alpha}",
                        get_sample_name(w->sample), w->window / 1000,
                        w->polling / 1000, w->value.a);
    }

}


static inline double calculate_alpha(double L, double n)
{
    double x, diff, min_x, min_diff;

    min_diff = 1.0;
    min_x    = 0.1;

    for (x = 0.01; x < 1.0; x += 0.001) {
        diff = pow(1 - x, n - 1) * x - L;
        if (fabs(diff) < min_diff) {
            min_x    = x;
            min_diff = fabs(diff);
        }
    }

    return min_x;
}


static inline void recalc_smoothing(mem_watch_lua_t *w)
{
    double alpha /*= 1 - exp(- (1.0 * w->polling) / (1.0 * w->window))*/;
    double n;

    if (w->window > w->polling) {
        n = (1.0 * w->window) / (1.0 * w->polling);
        alpha = calculate_alpha(0.0005, n);
    }
    else
        alpha = 1;

    ewma_init(&w->value, alpha, w->value.S);
}


void mem_watch_set_polling(mem_watch_lua_t *w, int polling)
{
    if (w->polling == polling)
        return;

    if (w->window != -1) {
        w->polling = polling;
        recalc_smoothing(w);
    }
}


void mem_watch_set_window(mem_watch_lua_t *w, int window)
{
    if (w->window == window)
        return;

    w->window = window;
    recalc_smoothing(w);
}


int mem_watch_update(mem_watch_lua_t *w, lua_State *L)
{
    int64_t sample = mem_get_sample(w->sample);
    int     change;

    if (sample < 0)
        return FALSE;


    if (w->update == NULL) {
        double       value = ewma_add(&w->value, sample);
        mem_limit_t *l;

        mrp_debug("%s sample=%d, estimate=%.2f", get_sample_name(w->sample),
                  sample, value);

        change = FALSE;
        for (l = w->limits; l->label != NULL; l++) {
            if (value <= l->limit) {
                if (w->curr != l) {
                    w->prev = w->curr;
                    w->curr = l;

                    change = TRUE;
                }

                break;
            }
        }
    }
    else {
        mrp_funcbridge_value_t args[2], rv;
        char                            rt;

        mrp_debug("%s sample=%d", get_sample_name(w->sample), sample);

        args[0].pointer = w;
        args[1].integer = sample;

        if (!mrp_funcbridge_call_from_c(L, w->update, "Od", &args[0], &rt,&rv)) {
            mrp_log_error("Failed to invoke memory watch update handler (%s).",
                          rv.string ? rv.string : "<unknown error>");
            mrp_free((char *)rv.string);
            change = FALSE;
        }
        else
            change = ((rt == MRP_FUNCBRIDGE_BOOLEAN && rv.boolean) ||
                      (rt == MRP_FUNCBRIDGE_INTEGER && rv.integer));
    }

    return change;
}


void mem_watch_notify(mem_watch_lua_t *w, lua_State *L)
{
    mrp_funcbridge_value_t args[3], rv;
    char                            rt;

    MRP_UNUSED(L);

    mrp_debug("memory watch %s: %s -> %s", get_sample_name(w->sample),
              w->prev ? w->prev->label : "<unknown>",
              w->curr ? w->curr->label : "<unknown>");

    if (w->notify == NULL)
        return;

    args[0].pointer = w;
    args[1].string  = w->prev ? w->prev->label : "<unknown>";
    args[2].string  = w->curr ? w->curr->label : "<unknown>";

    if (!mrp_funcbridge_call_from_c(L, w->notify, "Oss", &args[0], &rt, &rv)) {
        mrp_log_error("Failed to notify memory watch %s (%s).",
                      get_sample_name(w->sample),
                      rv.string ? rv.string : "<unknown error>");
        mrp_free((char *)rv.string);
    }
}


static inline mem_sample_t get_sample_id(const char *name)
{
#define MAP(_name, _id) if (!strcmp(name, _name)) return _id
    MAP("MemFree"   , MEM_SAMPLE_MEMFREE   );
    MAP("SwapFree"  , MEM_SAMPLE_SWAPFREE  );
    MAP("Dirty"     , MEM_SAMPLE_DIRTY     );
    MAP("Writeback" , MEM_SAMPLE_WRITEBACK );
#undef MAP

    return MEM_SAMPLE_INVALID;
}


static inline const char *get_sample_name(mem_sample_t id)
{
    const char *names[] = {
#define MAP(_name, _id) [_id] = _name
        MAP("MemFree"   , MEM_SAMPLE_MEMFREE  ),
        MAP("SwapFree"  , MEM_SAMPLE_SWAPFREE ),
        MAP("Dirty"     , MEM_SAMPLE_DIRTY    ),
        MAP("Writeback" , MEM_SAMPLE_WRITEBACK),
#undef MAP
    };

    if (MEM_SAMPLE_MEMFREE <= id && id <= MEM_SAMPLE_WRITEBACK)
        return names[id];
    else
        return "<invalid memory sample type>";
}


static int cmp_limits(const void *ptr1, const void *ptr2)
{
    mem_limit_t *l1 = (mem_limit_t *)ptr1;
    mem_limit_t *l2 = (mem_limit_t *)ptr2;

    return l1->limit - l2->limit;
}


static int get_limit(lua_State *L, int idx, mem_limit_t *l)
{
    char *u;

    if (lua_type(L, idx) != LUA_TTABLE)
        return -1;

    lua_getfield(L, idx, "label");

    if (lua_type(L, -1) == LUA_TSTRING)
        l->label = (char *)lua_tostring(L, -1);
    else
        l->label = NULL;

    lua_pop(L, 1);

    if (l->label == NULL)
        return -1;

    lua_getfield(L, idx, "limit");

    switch (lua_type(L, -1)) {
    case LUA_TNUMBER:
        l->limit = lua_tonumber(L, -1);
        break;
    case LUA_TSTRING:
        l->limit = strtoull(lua_tostring(L, -1), &u, 10);
        if (u[0]) {
            if (!u[1]) {
                switch (u[0]) {
                case 'k': l->limit *= 1024;           break;
                case 'M': l->limit *= 1024*1024;      break;
                case 'G': l->limit *= 1024*1024*1024; break;
                default:                              goto invalid;
                }
            }
            else
                goto invalid;
        }
        break;
    default:
    invalid:
        l->limit = -1;
        break;
    }

    lua_pop(L, 1);

    if (l->limit >= 0)
        return 0;
    else
        return -1;
}


static int setup_limits(mem_watch_lua_t *w, lua_State *L, int limref)
{
    int          top = lua_gettop(L);
    mem_limit_t *limits, l;
    int          nlimit;
    const char  *kname;
    int          ktype, i;
    size_t       klen;

    if (!mrp_lua_object_deref_value(w, L, limref, false)) {
        mrp_log_error("Failed to dereference memory watch limit table.");
        return 0;
    }

    limits = NULL;
    nlimit = 0;
    MRP_LUA_FOREACH_ALL(L, i, top + 1, ktype, kname, klen) {
        if (ktype != LUA_TNUMBER) {
            mrp_log_error("Invalid memory watch limits (non-numeric index).");
            goto fail;
        }

        if (get_limit(L, -1, &l) != 0) {
            mrp_log_error("Invalid memory watch limit #%zd.", klen);
            goto fail;
        }

        if (mrp_reallocz(limits, nlimit, nlimit + 1) == NULL) {
            mrp_log_error("Failed to allocate memory watch limits.");
            goto fail;
        }

        limits[nlimit].label = mrp_strdup(l.label);
        limits[nlimit].limit = l.limit;

        if (limits[nlimit].label == NULL) {
            mrp_log_error("memory watch limit with no or invalid label.");
            goto fail;
        }
        else
            nlimit++;
    }

    if (mrp_reallocz(limits, nlimit, nlimit + 1) == NULL)
        goto fail;

    qsort(limits, nlimit, sizeof(limits[0]), cmp_limits);

    cleanup_limits(w, L, w->limits, -1, w->limref);
    w->limits = limits;
    w->limref = limref;

    lua_settop(L, top);
    return 1;

 fail:
    cleanup_limits(w, L, limits, nlimit, LUA_NOREF);
    lua_settop(L, top);
    return 0;
}


static void cleanup_limits(mem_watch_lua_t *w, lua_State *L,
                           mem_limit_t *limits, int n, int limref)
{
    mem_limit_t *l;
    int          i;

    mrp_lua_object_unref_value(w, L, limref);

    if (limits == NULL)
        return;

    for (i = 0, l = limits; (n > 0 && i < n) || (n < 0 && l->label); i++, l++)
        mrp_free(l->label);

    mrp_free(limits);
}



/*
 * Uh... We misuse of the bindings registering macro by passing in
 * (the empty) { NULL, NULL } for the bindings and use its optional
 * class registering feature to register our class. Ugly..., we need
 * to add a similar MURPHY_REGISTER_LUA_CLASSES macro and the necessary
 * infra for it...
 */

MURPHY_REGISTER_LUA_BINDINGS(murphy, MEM_WATCH_LUA_CLASS, { NULL, NULL });
