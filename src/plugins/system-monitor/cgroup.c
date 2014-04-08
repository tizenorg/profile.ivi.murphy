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

#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <murphy/common/macros.h>
#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/list.h>
#include <murphy/common/refcnt.h>
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/core/lua-bindings/murphy.h>

#include "cpu-sampler.h"
#include "cgroup.h"

static char *roots[CGROUP_TYPE_MAX];     /* path for each cgroup type */
static MRP_LIST_HOOK(groups);            /* list of tracked groups */


/*
 * control file descriptors
 */

typedef struct {
    struct {                             /* common controls for all types */
        int tasks;                       /*     tasks */
        int procs;                       /*     cgroup.procs */
    } any;
    struct {                             /* memory-specific controls */
        int limit;                       /*     limit_in_bytes */
        int soft_limit;                  /*     soft_limit_in_bytes */
        int usage;                       /*     usage_in_bytes */
        int max_usage;                   /*     usage_in_bytes */
        int memsw_limit;                 /*     memsw_limit_in_bytes */
        int memsw_usage;                 /*     memsw_usage_in_bytes */
        int memsw_max_usage;             /*     memsw_usage_in_bytes */
        int swappiness;                  /*     swappiness */
        int stat;                        /*     stat */
    } memory;
    struct {                             /* cpuacct-specific controls */
        int usage_percpu;                /*     usage_percpu */
        int stat;                        /*     stat */
    } cpuacct;
    struct {                             /* cpu-specific controls */
        int shares;                      /*     shares */
        int cfs_period;                  /*     cfs_period_us */
        int cfs_quota;                   /*     cfs_quota_us */
        int rt_period;                   /*     rt_period_us */
        int rt_runtime;                  /*     rt_runtime_us */
    } cpu;
    struct {
        int state;
    } freezer;
} control_t;


/*
 * a known control group
 */

struct cgroup_s {
    mrp_list_hook_t  hook;               /* to list of cgroups */
    cgroup_type_t    type;               /* cgroup type */
    char            *name;               /* name of this group */
    char            *path;               /* full path in the filesystem */
    dev_t            dev;                /* device number for unique id */
    ino_t            ino;                /* inode number for unique id */
    mrp_refcnt_t     refcnt;             /* reference count */
    control_t        ctrl;               /* controlling fds */
};


/*
 * a cgroup control descriptor
 */

typedef enum {
    CONTROL_FORMAT_NONE    = 0x00,
    CONTROL_FORMAT_INTEGER = 0x01,       /* reads integer */
    CONTROL_FORMAT_STRING  = 0x02,       /* reads string */
    CONTROL_FORMAT_INTARR  = 0x03,       /* reads array of integers */
    CONTROL_FORMAT_STRARR  = 0x04,       /* reads array of strings */
    CONTROL_FORMAT_INTTBL  = 0x05,       /* reads table with integer values */
    CONTROL_FORMAT_STRTBL  = 0x06,       /* reads table with string values */
} control_format_t;

typedef enum {
    CONTROL_FLAG_NONE    = 0x00,
    CONTROL_FLAG_NOTTOP  = 0x01,         /* absent in controller root */
    CONTROL_FLAG_RDONLY  = 0x02,         /* read-only control */
    CONTROL_FLAG_WRONLY  = 0x04,         /* write-only control */
    CONTROL_FLAG_IGNORE  = 0x08,         /* don't try to open this */
} control_flag_t;

typedef struct {
    const char *alias;                   /* user-friendlier Lua alias */
    const char *path;                    /* relative path under mount point */
    off_t       offs;                    /* fd offset within control struct */
    int         flags;                   /* control-specific flags */
    int         format;                  /* format of data read from fd */
    int         missingok;               /* can be missing */
} control_descr_t;


#define SHARED  CONTROL_FLAG_SHARED
#define IGNORE  CONTROL_FLAG_IGNORE
#define MEMORY  (1 << CGROUP_TYPE_MEMORY )
#define CPUACCT (1 << CGROUP_TYPE_CPUACCT)
#define CPU     (1 << CGROUP_TYPE_CPU    )
#define FREEZER (1 << CGROUP_TYPE_FREEZER)
#define BLKIO   (1 << CGROUP_TYPE_BLKIO  )
#define NETCLS  (1 << CGROUP_TYPE_NETCLS )
#define DEVICES (1 << CGROUP_TYPE_DEVICES)


/*
 * common cgroup controller/filesytem entries
 */

#define ROCTRL(_alias, _path, _type, _field, _fmt, _miss)       \
    { _alias, #_path, MRP_OFFSET(control_t, _type._field),      \
            CONTROL_FLAG_RDONLY, CONTROL_FORMAT_##_fmt, _miss }
#define RWCTRL(_alias, _path, _type, _field, _fmt, _miss)       \
    { _alias, #_path, MRP_OFFSET(control_t, _type._field),      \
            CONTROL_FLAG_NONE, CONTROL_FORMAT_##_fmt, _miss   }

#define RO(_a, _path, _fld, _fmt) ROCTRL(_a, _path, any, _fld, _fmt, 0)
#define RW(_a, _path, _fld, _fmt) RWCTRL(_a, _path, any, _fld, _fmt, 0)

static control_descr_t common_controls[] = {
    RW("Tasks"    , tasks       , tasks, INTARR),
    RW("Processes", cgroup.procs, procs, INTARR),
    { NULL, NULL, -1, 0, 0, 0 }
};

#undef RO
#undef RW


/*
 * 'memory' cgroup controller/filesystem entries
 */

#define MRO(_a, _path, _fld, _fmt)                      \
    ROCTRL(_a, memory._path, memory, _fld, _fmt, 0)
#define MRW(_a, _path, _fld, _fmt)                      \
    RWCTRL(_a, memory._path, memory, _fld, _fmt, 0)

#define ORO(_a, _path, _fld, _fmt)                      \
    ROCTRL(_a, memory._path, memory, _fld, _fmt, 1)
#define ORW(_a, _path, _fld, _fmt)                      \
    RWCTRL(_a, memory._path, memory, _fld, _fmt, 1)

static control_descr_t memory_controls[] = {
    MRW("Limit"          , limit_in_bytes          , limit          , INTEGER),
    MRW("SoftLimit"      , soft_limit_in_bytes     , soft_limit     , INTEGER),
    MRO("Usage"          , usage_in_bytes          , usage          , INTEGER),
    MRO("MaxUsage"       , max_usage_in_bytes      , max_usage      , INTEGER),
    ORW("MemSwapLimit"   , memsw.limit_in_bytes    , memsw_limit    , INTEGER),
    ORO("MemSwapUsage"   , memsw.usage_in_bytes    , memsw_usage    , INTEGER),
    ORO("MemSwapMaxUsage", memsw.max_usage_in_bytes, memsw_max_usage, INTEGER),
    MRW("Swappiness"     , swappiness              , swappiness     , INTEGER),
    MRO("Stat"           , stat                    , stat           , INTTBL),
    { NULL, NULL, -1, 0, 0, 0 }
};

#undef RO
#undef RW


/*
 * 'cpuacct' cgroup controller/filesystem entries
 */

#define RO(_a, _path, _fld, _fmt) \
    ROCTRL(_a, cpuacct._path, cpuacct, _fld, _fmt, 0)
#define RW(_a, _path, _fld, _fmt) \
    RWCTRL(_a, cpuacct._path, cpuacct, _fld, _fmt, 0)

static control_descr_t cpuacct_controls[] = {
    RO("Usage", usage_percpu, usage_percpu, INTARR),
    RO("Stat" , stat        , stat        , INTTBL),
    { NULL, NULL, -1, 0, 0, 0 }
};

#undef RO
#undef RW


/*
 * 'cpu' cgroup controller/filesystem entries
 */

#define RO(_a, _path, _fld, _fmt) \
    ROCTRL(_a, cpu._path, cpu, _fld, _fmt, 0)
#define RW(_a, _path, _fld, _fmt) \
    RWCTRL(_a, cpu._path, cpu, _fld, _fmt, 0)

static control_descr_t cpu_controls[] = {
    RW("Shares"   , shares        , shares    , INTEGER),
    RW("CFSPeriod", cfs_period_us , cfs_period, INTEGER),
    RW("CFSQuota" , cfs_quota_us  , cfs_quota , INTEGER),
    RW("RTPeriod" , rt_period_us  , rt_period , INTEGER),
    RW("RTRuntime", rt_runtime_us , rt_runtime, INTEGER),
    { NULL, NULL, -1, 0, 0, 0 }
};

#undef RO
#undef RW


/*
 * 'freezer' cgroup controller/filesystem entries
 */

#define RO(_a, _path, _fld, _fmt)                       \
    ROCTRL(_a, freezer._path, freezer, _fld, _fmt, 0)
#define RW(_a, _path, _fld, _fmt)                       \
    RWCTRL(_a, freezer._path, freezer, _fld, _fmt, 0)

static control_descr_t freezer_controls[] = {
    RW("State", state, state, STRING),
    { NULL, NULL, -1, 0, 0, 0 }
};

#undef RO
#undef RW


/*
 * per controller filesystem entries
 */

static control_descr_t *controls[] = {
    [CGROUP_TYPE_MEMORY ] = memory_controls,
    [CGROUP_TYPE_CPU    ] = cpu_controls,
    [CGROUP_TYPE_CPUACCT] = cpuacct_controls,
    [CGROUP_TYPE_FREEZER] = freezer_controls,
};



/*
 * cgroup type names
 */

static const char *type_names[] = {
    [CGROUP_TYPE_MEMORY ] = "memory",
    [CGROUP_TYPE_CPUACCT] = "cpuacct",
    [CGROUP_TYPE_CPU    ] = "cpu",
    [CGROUP_TYPE_CPUSET ] = "cpuset",
    [CGROUP_TYPE_FREEZER] = "freezer",
    [CGROUP_TYPE_BLKIO  ] = "blkio",
    [CGROUP_TYPE_NETCLS ] = "net_cls",
    [CGROUP_TYPE_DEVICES] = "devices",
    NULL
};


static inline cgroup_type_t cgroup_type(const char *type_name)
{
    cgroup_type_t type;

    for (type = 0; type_names[type] != NULL; type++)
        if (!strcmp(type_names[type], type_name))
            return type;

    return CGROUP_TYPE_UNKNOWN;
}


static const char *find_mount_point(cgroup_type_t type)
{
#define IS_TYPE(_type)                                                  \
            (strlen(_type) == l &&                                      \
             !strncmp(o, _type, l) && (o[l] == ',' || !o[l]))

#define SAVE_PATH(_type) do {                                           \
                char *_beg = path;                                      \
                char *_end = fstype;                                    \
                int   _len = _end - _beg;                               \
                                                                        \
                mrp_free(roots[_type]);                                 \
                                                                        \
                if ((roots[_type] = mrp_datadup(_beg, _len)) != NULL)   \
                    roots[_type][_len - 1] = '\0';                      \
            } while (0)

    static int  discovered = FALSE;
    char        mounts[8192], *p, *n;
    char       *dev, *path, *fstype, *options, *freq, *o, *e;
    int         fd, l;

    if (discovered)
        return roots[type];

    if ((fd = open("/proc/mounts", O_RDONLY)) < 0)
        return NULL;

    if ((l = read(fd, mounts, sizeof(mounts) - 1)) <= 0)
        return NULL;
    else
        mounts[l] = '\0';

    p = mounts;
    while (p && *p) {
        n   = strchr(p, '\n');
        dev = p;

        if ((path = strchr(dev, ' ')) == NULL)
            goto next;
        else
            path++;

        if ((fstype = strchr(path, ' ')) == NULL)
            goto next;
        else
            fstype++;

        if (strncmp(fstype, "cgroup ", 7) != 0)
            goto next;

        if ((options = strchr(fstype, ' ')) == NULL)
            goto next;
        else
            options++;

        if ((freq = strchr(options, ' ')) == NULL)
            goto next;
        else
            *freq = '\0';

        o = options;
        while (o && *o) {
            e = strchr(o, ',');
            l = e ? e - o : (int)strlen(o);

            if      (IS_TYPE("memory" )) SAVE_PATH(CGROUP_TYPE_MEMORY );
            else if (IS_TYPE("cpuacct")) SAVE_PATH(CGROUP_TYPE_CPUACCT);
            else if (IS_TYPE("cpu"    )) SAVE_PATH(CGROUP_TYPE_CPU    );
            else if (IS_TYPE("cpuset" )) SAVE_PATH(CGROUP_TYPE_CPUSET );
            else if (IS_TYPE("devices")) SAVE_PATH(CGROUP_TYPE_DEVICES);
            else if (IS_TYPE("freezer")) SAVE_PATH(CGROUP_TYPE_FREEZER);
            else if (IS_TYPE("blkio"  )) SAVE_PATH(CGROUP_TYPE_BLKIO  );
            else if (IS_TYPE("net_cls")) SAVE_PATH(CGROUP_TYPE_NETCLS );

            o = (e && *e) ? e + 1 : NULL;
        }

    next:
        if ((p = n) != NULL)
            p++;
    }

    discovered = TRUE;

    return roots[type];

#undef IS_TYPE
#undef SAVE_PATH
}


static void close_controls(cgroup_t *cgrp)
{
    control_descr_t *ctrl;
    int             *fdp, type;

    for (type = -1; type < CGROUP_TYPE_MAX; type++) {
        if (type >= 0) {
            if (!(cgrp->type & (1 << type)))
                continue;

            if ((ctrl = controls[type]) == NULL)
                continue;
        }
        else
            ctrl = common_controls;

        while (ctrl->path != NULL) {
            fdp = (int *)((void *)&cgrp->ctrl + ctrl->offs);

            if (*fdp >= 0) {
                close(*fdp);
                *fdp = 0;
            }

            ctrl++;
        }
    }
}


static int open_controls(cgroup_t *cgrp, cgroup_type_t type, int grpflags)
{
    control_descr_t *ctrl;
    int             *fdp, flags;
    char             path[PATH_MAX];

    if (type >= 0) {
        if ((ctrl = controls[type]) == NULL) {
            errno = EINVAL;
            return -1;
        }
    }
    else
        ctrl = common_controls;

    while (ctrl->path != NULL) {
        fdp   = (int *)((void *)&cgrp->ctrl + ctrl->offs);
        flags = (ctrl->flags & CONTROL_FLAG_RDONLY) ? O_RDONLY : O_RDWR;

        if (*fdp >= 0)
            goto next;

        if (snprintf(path, sizeof(path), "%s/%s",
                     cgrp->path, ctrl->path) >= (int)sizeof(path))
            return -1;

    retry:
        mrp_debug("attempting to open '%s' (in %s mode)", path,
                  flags == O_RDONLY ? "read-only" : "read-write");

        if ((*fdp = open(path, flags)) < 0) {
            mrp_debug("failed: %d (%s)", errno, strerror(errno));
            if (errno == ENOENT) {
                if (ctrl->missingok) {
                    *fdp = -2;
                    goto next;
                }
                else
                    return -1;
            }
            if (errno != EACCES)
                return -1;
            if (flags == O_RDONLY)
                return -1;
            if (grpflags & O_RDWR)
                return -1;
            flags = O_RDONLY;
            goto retry;
        }

    next:
        ctrl++;
    }

    if (cgrp->dev == 0 && cgrp->ino == 0) {
        struct stat st;

        if (stat(cgrp->path, &st) == 0) {
            cgrp->dev = st.st_dev;
            cgrp->ino = st.st_ino;
        }
    }

    cgrp->type |= (1 << type);

    return 0;
}


static cgroup_t *find_cgroup(const char *path)
{
    mrp_list_hook_t *p, *n;
    cgroup_t        *cgrp;
    struct stat      st;

    if (stat(path, &st) != 0)
        return NULL;

    mrp_list_foreach(&groups, p, n) {
        cgrp = mrp_list_entry(p, typeof(*cgrp), hook);

        if (cgrp->dev == st.st_dev && cgrp->ino == st.st_ino)
            return cgrp;
    }

    return NULL;
}


cgroup_t *cgroup_ref(cgroup_t *cgrp)
{
    return mrp_ref_obj(cgrp, refcnt);
}


int cgroup_unref(cgroup_t *cgrp)
{
    if (mrp_unref_obj(cgrp, refcnt)) {
        mrp_list_delete(&cgrp->hook);

        mrp_free(cgrp->name);
        mrp_free(cgrp->path);
        close_controls(cgrp);

        mrp_free(cgrp);

        return TRUE;
    }
    else
        return FALSE;
}


cgroup_t *cgroup_open(cgroup_type_t type, const char *name, int flags)
{
    const char *root;
    char        path[PATH_MAX];
    cgroup_t   *cgrp;

    if (type < 0 || type >= CGROUP_TYPE_MAX) {
        errno = EINVAL;
        return NULL;
    }

    if ((root = find_mount_point(type)) == NULL) {
        mrp_log_error("Couldn't find mount point for cgroup type %s.",
                      type_names[type]);
        errno = ENOENT;
        return NULL;
    }

    if (snprintf(path, sizeof(path), "%s/%s", root, name) >= (int)sizeof(path)){
        errno = EOVERFLOW;
        return NULL;
    }

    if (flags & O_CREAT) {
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            mrp_log_error("Couldn't create cgroup '%s' (%d: %s).",
                          path, errno, strerror(errno));
            return NULL;
        }
    }

    if ((cgrp = find_cgroup(path)) != NULL) {
        if (open_controls(cgrp, type, flags) == 0)
            return cgroup_ref(cgrp);
        else
            return NULL;
    }

    if ((cgrp = mrp_allocz(sizeof(*cgrp))) == NULL)
        return NULL;

    mrp_list_init(&cgrp->hook);
    mrp_refcnt_init(&cgrp->refcnt);
    memset(&cgrp->ctrl, -1, sizeof(cgrp->ctrl));

    cgrp->name = mrp_strdup(name);
    cgrp->path = mrp_strdup(path);

    if (cgrp->name == NULL || cgrp->path == NULL)
        goto fail;

    if (open_controls(cgrp, -1  , flags) != 0 ||
        open_controls(cgrp, type, flags) != 0)
        goto fail;

    mrp_list_append(&groups, &cgrp->hook);

    return cgrp;

 fail:
    cgroup_unref(cgrp);

    return NULL;
}


const char *cgroup_get_path(cgroup_t *cgrp)
{
    return cgrp ? cgrp->path : "<no cgroup>";
}


const char *cgroup_get_name(cgroup_t *cgrp)
{
    return cgrp ? cgrp->name : "<no cgroup>";
}


int cgroup_get_cpu_usage(cgroup_t *cgrp, uint64_t *usgbuf, size_t n)
{
    char buf[512], *p;
    int  len, fd, i;

    if (!(cgrp->type & (1 << CGROUP_TYPE_CPUACCT)))
        return -1;

    fd = cgrp->ctrl.cpuacct.usage_percpu;

    if (lseek(fd, 0, SEEK_SET) != 0)
        return -1;

    if ((len = read(fd, buf, sizeof(buf) - 1)) <= 0)
        return -1;
    else
        buf[len] = '\0';

    p = buf;
    i = 1;
    usgbuf[0] = 0;
    while (i < (int)n && *p) {
        usgbuf[0] += (usgbuf[i] = strtoull(p, &p, 10));
        if (*p) {
            if (*p != ' ')
                return -1;
            else
                p++;
        }

        i++;
    }

    return (i == (int)n ? 0 : -1);
}


/*
 * CGroup Lua object
 */

#define CGROUP_LUA_CLASS MRP_LUA_CLASS(cgroup, lua)
#define RO               MRP_LUA_CLASS_READONLY
#define NOINIT           MRP_LUA_CLASS_NOINIT
#define NOFLAGS          MRP_LUA_CLASS_NOFLAGS
#define setmember        cgroup_lua_setmember
#define getmember        cgroup_lua_getmember

typedef enum {
    CGROUP_MEMBER_TYPE,
    CGROUP_MEMBER_NAME,
    CGROUP_MEMBER_MODE,
} cgroup_member_t;

typedef struct {
    char     *type;                      /* cgroup type */
    char     *name;                      /* cgroup name */
    char     *mode;                      /* mode flags */
    cgroup_t *cg;                        /* actual cgroup */
} cgroup_lua_t;


static int cgroup_lua_no_constructor(lua_State *L);
static void cgroup_lua_destroy(void *data);
static void cgroup_lua_changed(void *data, lua_State *L, int member);
static ssize_t cgroup_lua_tostring(mrp_lua_tostr_mode_t mode, char *buf,
                                   size_t size, lua_State *L, void *data);
static int cgroup_lua_setmember(void *data, lua_State *L, int member,
                                mrp_lua_value_t *v);
static int cgroup_lua_getmember(void *data, lua_State *L, int member,
                                mrp_lua_value_t *v);
static int cgroup_lua_setfield(lua_State *L);
static int cgroup_lua_getfield(lua_State *L);
static int cgroup_lua_open(lua_State *L);
static int cgroup_lua_close(lua_State *L);
static int cgroup_lua_addtask(lua_State *L);
static int cgroup_lua_addproc(lua_State *L);
static int cgroup_lua_setparam(lua_State *L);
static int push_formatted(lua_State *L, int fmt, char *data);

MRP_LUA_METHOD_LIST_TABLE(cgroup_lua_methods,
    MRP_LUA_METHOD_CONSTRUCTOR(cgroup_lua_no_constructor)
    MRP_LUA_METHOD(close      , cgroup_lua_close   )
    MRP_LUA_METHOD(add_task   , cgroup_lua_addtask )
    MRP_LUA_METHOD(add_process, cgroup_lua_addproc )
    MRP_LUA_METHOD(set_param  , cgroup_lua_setparam));

MRP_LUA_METHOD_LIST_TABLE(cgroup_lua_overrides,
    MRP_LUA_OVERRIDE_CALL(cgroup_lua_no_constructor)
    MRP_LUA_OVERRIDE_SETFIELD(cgroup_lua_setfield)
    MRP_LUA_OVERRIDE_GETFIELD(cgroup_lua_getfield));

MRP_LUA_MEMBER_LIST_TABLE(cgroup_lua_members,
    MRP_LUA_CLASS_STRING ("type", 0, setmember, getmember, RO)
    MRP_LUA_CLASS_STRING ("name", 0, setmember, getmember, RO)
    MRP_LUA_CLASS_STRING ("mode", 0, setmember, getmember, RO));

MRP_LUA_DEFINE_CLASS(cgroup, lua, cgroup_lua_t, cgroup_lua_destroy,
    cgroup_lua_methods, cgroup_lua_overrides, cgroup_lua_members, NULL,
    cgroup_lua_changed, cgroup_lua_tostring , NULL,
                     MRP_LUA_CLASS_EXTENSIBLE | MRP_LUA_CLASS_PRIVREFS);

MRP_LUA_CLASS_CHECKER(cgroup_lua_t, cgroup_lua, CGROUP_LUA_CLASS);

static int cgroup_lua_no_constructor(lua_State *L)
{
    return luaL_error(L, "can't create CGroup via constructor, "
                      "use create or open instead");
}


static void cgroup_lua_destroy(void *data)
{
    cgroup_lua_t *cgrp = (cgroup_lua_t *)data;

    mrp_free(cgrp->type);
    mrp_free(cgrp->name);
    mrp_free(cgrp->mode);

    cgrp->type = cgrp->name = cgrp->mode = NULL;
}


static int parse_mode(const char *mode)
{
#define MATCHES(_key) (len == sizeof(_key) - 1 && !strncmp(opt, _key, len))
    const char *opt, *e;
    int         len, flags;

    opt   = mode;
    flags = O_RDWR;

    while (opt && *opt) {
        if ((e = strchr(opt, ',')) != NULL)
            len = e - opt;
        else
            len = strlen(opt);

        if (MATCHES("readonly") || MATCHES("ro")) {
            flags &= ~O_RDWR;
            flags |= O_RDONLY;
        }
        else if (MATCHES("readwrite") || MATCHES("rw"))
            flags |= O_RDWR;
        else if (MATCHES("create"))
            flags |= O_CREAT | O_RDWR;
        else
            return -1;

        opt = e && *e ? e + 1 : NULL;
    }

    return flags;
#undef MATCHES
}


static int cgroup_lua_open(lua_State *L)
{
    cgroup_lua_t *cgrp;
    const char   *type, *name, *mode;
    int           narg, flags;
    char          e[256];

    if ((narg = lua_gettop(L)) != 2)
        return luaL_error(L, "expecting 1 argument");

    luaL_checktype(L, 2, LUA_TTABLE);

    lua_pushstring(L, "type");
    lua_gettable(L, 2);

    if (lua_type(L, -1) != LUA_TSTRING)
        return luaL_error(L, "field 'type' not specified for CGroup.");

    type = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_pushstring(L, "name");
    lua_gettable(L, 2);

    if (lua_type(L, -1) != LUA_TSTRING)
        return luaL_error(L, "field 'name' not specified for CGroup.");

    name = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_pushstring(L, "mode");
    lua_gettable(L, 2);

    if (lua_type(L, -1) != LUA_TSTRING)
        return luaL_error(L, "field 'mode' not specified for CGroup.");

    mode = lua_tostring(L, -1);
    lua_pop(L, 1);

    if ((flags = parse_mode(mode)) < 0)
        return luaL_error(L, "invalid mode '%s'", mode);

    cgrp = (cgroup_lua_t *)mrp_lua_create_object(L, CGROUP_LUA_CLASS, NULL, 0);

    cgrp->cg = cgroup_open(cgroup_type(type), name, flags);

    if (cgrp->cg == NULL) {
        lua_pop(L, 1);
        return 0;
    }

    cgrp->type = mrp_strdup(type);
    cgrp->name = mrp_strdup(name);
    cgrp->mode = mrp_strdup(mode);

    if (mrp_lua_init_members(cgrp, L, 2, e, sizeof(e)) != 1) {
        luaL_error(L, "failed to initialize CGroup (error: %s)",
                   *e ? e : "<unknown error>");
        lua_pop(L, 1);
        return 0;
    }
    else
        return 1;
}


static int cgroup_lua_close(lua_State *L)
{
    cgroup_lua_t *cgrp = cgroup_lua_check(L, 1);

    cgroup_unref(cgrp->cg);
    cgrp->cg = NULL;

    return 0;
}


static void cgroup_lua_changed(void *data, lua_State *L, int member)
{
    MRP_UNUSED(data);
    MRP_UNUSED(L);
    MRP_UNUSED(member);
}


static ssize_t cgroup_lua_tostring(mrp_lua_tostr_mode_t mode, char *buf,
                                   size_t size, lua_State *L, void *data)
{
    cgroup_lua_t *cgrp = (cgroup_lua_t *)data;

    MRP_UNUSED(L);

    switch (mode & MRP_LUA_TOSTR_MODEMASK) {
    case MRP_LUA_TOSTR_LUA:
    default:
        return snprintf(buf, size, "{CGroup %s/%s (%s), %p}",
                        cgrp->type, cgrp->name,
                        cgrp->cg ? cgrp->cg->path : "<unknown>", cgrp);
    }
}


static int cgroup_lua_setmember(void *data, lua_State *L, int member,
                                mrp_lua_value_t *v)
{
    cgroup_lua_t *cgrp = (cgroup_lua_t *)data;

    mrp_debug("fake setting cgroup member #%d", member);

    MRP_UNUSED(L);
    MRP_UNUSED(member);
    MRP_UNUSED(v);
    MRP_UNUSED(cgrp);

    return 1;
}


static int cgroup_lua_getmember(void *data, lua_State *L, int member,
                                mrp_lua_value_t *v)
{
    cgroup_lua_t *cgrp = (cgroup_lua_t *)data;

    MRP_UNUSED(L);

    mrp_debug("getting cgroup member #%d", member);

    switch (member) {
    case CGROUP_MEMBER_TYPE: v->str = cgrp->type; return 1;
    case CGROUP_MEMBER_NAME: v->str = cgrp->name; return 1;
    case CGROUP_MEMBER_MODE: v->str = cgrp->mode; return 1;
    }

    return 0;
}


static int get_control_fd(cgroup_t *cg, const char *name, int type, int *fmtp)
{
    control_descr_t *ctrl;

    if (type >= 0) {
        if ((ctrl = controls[type]) == NULL)
            return -1;
    }
    else
        ctrl = common_controls;

    while (ctrl->path != NULL) {
        if (!strcmp(name, ctrl->alias) || !strcmp(name, ctrl->path)) {
            if (fmtp != NULL)
                *fmtp = ctrl->format;
            return *(int *)((void *)&cg->ctrl + ctrl->offs);
        }
        else
            ctrl++;
    }

    return -1;
}


static int get_cgroup_fd(cgroup_t *cg, const char *name, int *fmtp)
{
    int type, fd;

    if (cg == NULL || name == NULL)
        return -1;

    for (type = -1; type < CGROUP_TYPE_MAX; type++) {
        if (type >= 0 && !(cg->type & (1 << type)))
            continue;
        if ((fd = get_control_fd(cg, name, type, fmtp)) >= 0)
            return fd;
    }

    return -1;
}


static int cgroup_lua_setfield(lua_State *L)
{
    cgroup_lua_t *cgrp = cgroup_lua_check(L, -3);
    const char   *name, *val;
    char          buf[512];
    int           fd, len;

    luaL_checktype(L, -2, LUA_TSTRING);
    name = lua_tostring(L, -2);

    mrp_debug("setting cgroup field '%s'", name);

    if ((fd = get_cgroup_fd(cgrp->cg, name, NULL)) < 0)
        return (fd == -2);               /* missing optional entry */

    mrp_debug("control fd for field '%s' is %d", name, fd);

    switch (lua_type(L, -1)) {
    case LUA_TSTRING:
        val = lua_tostring(L, -1);
        len = strlen(val);
        break;
    case LUA_TNUMBER:
        len = snprintf(buf, sizeof(buf), "%.0f", lua_tonumber(L, -1));
        val = buf;
        break;
    default:
        return luaL_error(L, "expecting string or integer value");
    }

    mrp_debug("writing value '%s' for field '%s'", val, name);

    if (write(fd, val, len) == len)
        return 1;
    else
        return -1;
}


static int cgroup_lua_getfield(lua_State *L)
{
    cgroup_lua_t *cgrp = cgroup_lua_check(L, -2);
    const char   *name;
    char          buf[4096];
    int           fd, len, fmt;

    luaL_checktype(L, -1, LUA_TSTRING);
    name = lua_tostring(L, -1);

    mrp_debug("getting cgroup field '%s'", name);

    if ((fd = get_cgroup_fd(cgrp->cg, name, &fmt)) < 0)
        return 0;

    mrp_debug("control fd for field '%s' is %d", name, fd);

    len = read(fd, buf, sizeof(buf) - 1);
    lseek(fd, 0, SEEK_SET);

    if (len < 0) {
        lua_pushnil(L);
        return 1;
    }
    else {
        buf[len] = '\0';
        while (len >= 1 && buf[len-1] == '\n')
            buf[--len] = '\0';

        mrp_debug("value for field '%s': '%s'", name, buf);

        return push_formatted(L, fmt, buf);
    }
}


static inline int write_pid(int fd, unsigned int pid)
{
    char buf[64];
    int  len;

    len = snprintf(buf, sizeof(buf), "%u", pid);

    if (len <= 0) {
        errno = EINVAL;
        return -1;
    }

    if (write(fd, buf, len) == len)
        return 0;
    else
        return -1;
}


static int cgroup_lua_addpid(lua_State *L, int process)
{
    cgroup_lua_t *cgrp = cgroup_lua_check(L, 1);
    int           type, fd, pid, i, status;
    const char   *name;
    size_t        idx;

    if (lua_gettop(L) != 2)
        return luaL_error(L, "expecting 1 argument, got %d", lua_gettop(L) - 1);

    if ((type = lua_type(L, 2)) != LUA_TNUMBER && type != LUA_TTABLE)
        return luaL_error(L, "expecting integer or table argument");

    if (cgrp->cg == NULL)
        return luaL_error(L, "given CGroup has no controls open");

    fd = process ? cgrp->cg->ctrl.any.procs : cgrp->cg->ctrl.any.tasks;

    if (fd < 0)
        return luaL_error(L, "given CGroup has no task/process control open");

    if (type == LUA_TNUMBER) {           /* a single PID */
        pid = lua_tointeger(L, 2);

        if (pid != lua_tonumber(L, 2))
            return luaL_error(L, "non-integer number given as PID");

        if (write_pid(fd, pid) != 0)
            lua_pushinteger(L, -1);
        else
            lua_pushinteger(L, 0);
        return 1;
    }
                                         /* a table of PIDs */
    status = 1;
    MRP_LUA_FOREACH_ALL(L, i, 2, type, name, idx) {
        if (type != LUA_TNUMBER)
            return luaL_error(L, "expecting pure table of integer PIDs");

        if (lua_type(L, -1) != LUA_TNUMBER)
            return luaL_error(L, "expecting table of integer PIDs");

        pid = lua_tointeger(L, -1);

        if (pid != lua_tonumber(L, -1))
            return luaL_error(L, "non-integer number given as PID");

        if (write_pid(fd, pid) != 0)
            if (status > 0)
                status = -(i + 1);
    }

    lua_pushinteger(L, status);
    return 1;
}


static int cgroup_lua_addtask(lua_State *L)
{
    return cgroup_lua_addpid(L, FALSE);
}


static int cgroup_lua_addproc(lua_State *L)
{
    return cgroup_lua_addpid(L, TRUE);
}


static int cgroup_lua_getparam(lua_State *L)
{
    return cgroup_lua_getfield(L);
}


static int cgroup_lua_setparam(lua_State *L)
{
    return cgroup_lua_setfield(L);
}


static inline int push_string(lua_State *L, char *data)
{
    lua_pushstring(L, data);
    return 1;
}


static inline int push_integer(lua_State *L, char *data)
{
    double  dbl;
    char   *e;

    dbl = strtod(data, &e);

    if (e && !*e) {
        lua_pushnumber(L, dbl);
        return 1;
    }
    else
        return -1;
}


static int push_intarr(lua_State *L, char *data)
{
    char   *p, *e;
    double  dbl;
    int     i;

    lua_newtable(L);

    p = data;
    i = 1;
    while (p && *p) {
        dbl = strtod(p, &e);

        if (e && *e != ' ' && *e != '\n' && *e != '\0') {
            lua_pop(L, 1);
            return -1;
        }

        lua_pushnumber(L, dbl);
        lua_rawseti(L, -2, i);
        i++;

        if (e && *e) {
            p = e + 1;
            while (isspace(*p))
                p++;
        }
        else
            p = NULL;
    }

    return 1;
}


static int push_strarr(lua_State *L, char *data)
{
    char   *p, *e;
    int     i;
    size_t  l;

    lua_newtable(L);

    p = data;
    i = 1;
    while (p && *p) {
        e = strchr(p, ' ');

        if (e && *e != ' ' && *e != '\n') {
            lua_pop(L, 1);
            return -1;
        }

        l = e ? (size_t)(e - p) : strlen(p);
        lua_pushlstring(L, p, l);
        lua_rawseti(L, -2, i);
        i++;

        if (e && *e) {
            p = e + 1;
            while (isspace(*p))
                p++;
        }
        else
            p = NULL;
    }

    return 1;
}


static int push_inttbl(lua_State *L, char *data)
{
    char   *p, *e;
    char   *key;
    double  val;

    lua_newtable(L);

    p = data;

    while (p && *p) {
        key = p;
        e   = strchr(p, ' ');

        if (e == NULL) {
            lua_pop(L, 1);
            return -1;
        }

        lua_pushlstring(L, key, e - key);

        val = strtod(e + 1, &e);

        if (e && *e != '\n' && *e != '\0') {
            lua_pop(L, 2);
            return -1;
        }

        lua_pushnumber(L, val);
        lua_rawset(L, -3);

        p = (e && *e) ? e + 1 : NULL;
    }

    return 1;
}


static int push_strtbl(lua_State *L, char *data)
{
    char   *p, *e;
    char   *key, *val;
    size_t  l;

    lua_newtable(L);

    p = data;

    while (p && *p) {
        key = p;
        e   = strchr(p, ' ');

        if (e == NULL) {
            lua_pop(L, 1);
            return -1;
        }

        lua_pushlstring(L, key, e - key);

        val = e + 1;
        e   = strchr(val, '\n');
        l   = e ? (size_t)(e - val) : strlen(val);

        lua_pushlstring(L, val, l);
        lua_rawset(L, -3);

        p = (e && *e) ? e + 1 : NULL;
    }

    return 1;
}


static int push_formatted(lua_State *L, int fmt, char *data)
{
    switch ((control_format_t)fmt) {
    default:
    case CONTROL_FORMAT_INTEGER: return push_integer(L, data);
    case CONTROL_FORMAT_STRING:  return push_string (L, data);
    case CONTROL_FORMAT_INTARR:  return push_intarr (L, data);
    case CONTROL_FORMAT_STRARR:  return push_strarr (L, data);
    case CONTROL_FORMAT_INTTBL:  return push_inttbl (L, data);
    case CONTROL_FORMAT_STRTBL:  return push_strtbl (L, data);
    }
}


MURPHY_REGISTER_LUA_BINDINGS(murphy, CGROUP_LUA_CLASS,
                             { "CGroupOpen", cgroup_lua_open });
