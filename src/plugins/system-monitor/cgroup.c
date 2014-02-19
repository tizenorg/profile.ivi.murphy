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

#include "cpu-sampler.h"
#include "cgroup.h"

static char *roots[CGROUP_TYPE_MAX];     /* path for each cgroup type */
static MRP_LIST_HOOK(groups);            /* list of tracked groups */


/*
 * control file descriptors
 */

typedef union {
    struct {                             /* common controls for all types */
        int tasks;                       /*     tasks */
    } any;
    struct {                             /* memory-specific controls */
        int tasks;                       /*     tasks */
        int limit;                       /*     limit_in_bytes */
        int usage;                       /*     usage_in_bytes */
        int soft_limit;                  /*     soft_limit_in_bytes */
        int memsw_limit;                 /*     memsw_limit_in_bytes */
        int memsw_usage;                 /*     memsw_usage_in_bytes */
        int swappiness;                  /*     swappiness */
    } memory;
    struct {                             /* cpuacct-specific controls */
        int tasks;                       /*     tasks */
        int usage_percpu;                /*     usage_percpu */
    } cpuacct;
    struct {                             /* cpu-specific controls */
        int tasks;                       /*     tasks */
        int cfs_period;                  /*     cfs_period_us */
        int cfs_quota;                   /*     cfs_quota_us */
        int rt_period;                   /*     rt_period_us */
        int rt_runtime;                  /*     rt_runtime_us */
    } cpu;
} control_t;


/*
 * a tracked control group
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
 * a group-specific control path
 */

typedef struct {
    const char *path;                    /* relative path within group */
    off_t       offs;                    /* offset within control_t */
    int         rdwr;                    /* whether read-write */
} control_path_t;


#define ROPATH(_path, _field)                           \
    { _path, MRP_OFFSET(control_t, _field), FALSE }
#define RWPATH(_path, _field)                           \
    { _path, MRP_OFFSET(control_t, _field), TRUE  }

static control_path_t memory_controls[] = {
    RWPATH("tasks"                      , any.tasks          ),
    RWPATH("memory.limit_in_bytes"      , memory.limit       ),
    RWPATH("memory.soft_limit_in_bytes" , memory.soft_limit  ),
    RWPATH("memory.memsw.limit_in_bytes", memory.memsw_limit ),
    ROPATH("memory.memsw.usage_in_bytes", memory.memsw_usage ),
    RWPATH("memory.swappiness"          , memory.swappiness  ),
    { NULL, 0, 0 },
};

static control_path_t cpuacct_controls[] = {
    RWPATH("tasks"                      , any.tasks          ),
    ROPATH("cpuacct.usage_percpu"      , cpuacct.usage_percpu),
    { NULL, 0, 0 },
};

static control_path_t cpu_controls[] = {
    RWPATH("tasks"                      , any.tasks          ),
    RWPATH("cfs_period_us"              , cpu.cfs_period     ),
    RWPATH("cfs_quota_us"               , cpu.cfs_quota      ),
    RWPATH("rt_period_us"               , cpu.rt_period      ),
    RWPATH("rt_runtime_us"              , cpu.rt_runtime     ),
    { NULL, 0, 0 },
};

static control_path_t *controls[] = {
    [CGROUP_TYPE_MEMORY ] = memory_controls,
    [CGROUP_TYPE_CPU    ] = cpu_controls,
    [CGROUP_TYPE_CPUACCT] = cpuacct_controls,
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
};



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
    control_path_t *paths;
    int            *fdp, i;

    paths = controls[cgrp->type];

    if (paths == NULL)
        return;

    for (i = 0; paths[i].path != NULL; i++) {
        fdp = (int *)((void *)&cgrp->ctrl + paths[i].offs);

        if (*fdp >= 0) {
            close(*fdp);
            *fdp = 0;
        }
    }
}


static int open_controls(cgroup_t *cgrp, int flags)
{
    control_path_t *paths;
    int            *fdp, i;
    char            path[PATH_MAX];

    paths = controls[cgrp->type];

    if (paths == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; paths[i].path != NULL; i++) {
        if (flags == O_RDONLY && paths[i].rdwr)
            continue;
        else
            fdp = (int *)((void *)&cgrp->ctrl + paths[i].offs);

        if (*fdp >= 0)
            continue;

        if (snprintf(path, sizeof(path), "%s/%s",
                     cgrp->path, paths[i].path) >= (int)sizeof(path))
            goto fail;

        if ((*fdp = open(path, flags)) < 0) {
        fail:
            close_controls(cgrp);
            return -1;
        }
    }

    if (cgrp->dev == 0 && cgrp->ino == 0) {
        struct stat st;

        if (stat(cgrp->path, &st) == 0) {
            cgrp->dev = st.st_dev;
            cgrp->ino = st.st_ino;
        }
    }

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
    return mrp_unref_obj(cgrp, refcnt);
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
        return NULL;
    }

    if (snprintf(path, sizeof(path), "%s/%s", root, name) >= (int)sizeof(path))
        goto fail;

    if ((cgrp = find_cgroup(path)) != NULL) {
        if (open_controls(cgrp, flags) == 0)
            return cgroup_ref(cgrp);
        else
            return NULL;
    }

    if ((cgrp = mrp_allocz(sizeof(*cgrp))) == NULL)
        return NULL;

    mrp_list_init(&cgrp->hook);
    mrp_refcnt_init(&cgrp->refcnt);
    memset(&cgrp->ctrl, -1, sizeof(cgrp->ctrl));

    cgrp->type = type;
    cgrp->name = mrp_strdup(name);
    cgrp->path = mrp_strdup(path);

    if (cgrp->name == NULL || cgrp->path == NULL)
        goto fail;

    if (open_controls(cgrp, flags) != 0)
        goto fail;

    mrp_list_append(&groups, &cgrp->hook);

    return cgrp;

 fail:
    mrp_free(cgrp->name);
    mrp_free(cgrp->path);
    mrp_free(cgrp);

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

    if (cgrp->type != CGROUP_TYPE_CPUACCT)
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
        usgbuf[0] += (usgbuf[i]  = strtoull(p, &p, 10));
        if (*p) {
            if (*p != ' ')
                return -1;
            else
                p++;
        }

        i++;
    }

    return i;
}
