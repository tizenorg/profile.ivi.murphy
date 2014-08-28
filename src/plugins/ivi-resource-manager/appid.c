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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>

#include <tzplatform_config.h>

#include <murphy/common.h>

#include "appid.h"

#define APP_MAX   1024

typedef struct appid_map_s   appid_map_t;

struct appid_map_s {
    const char *id;
    const char *exe;
};

struct mrp_resmgr_appid_s {
    mrp_resmgr_data_t *data;
    mrp_htbl_t *map;
};

static void map_init(mrp_resmgr_appid_t *, const char *);
static void map_add_entry(mrp_resmgr_appid_t *, const char *, const char *);
static void map_free_entry(void *, void *);

static int pid2exe(const char *, char *, size_t);


mrp_resmgr_appid_t *mrp_resmgr_appid_create(mrp_resmgr_data_t *data)
{
    mrp_resmgr_appid_t *appid;
    mrp_htbl_config_t cfg;
    char *app_dir = tzplatform_getenv(TZ_USER_APP);

    cfg.nentry = APP_MAX;
    cfg.comp = mrp_string_comp;
    cfg.hash =  mrp_string_hash;
    cfg.free = map_free_entry;
    cfg.nbucket = cfg.nentry / 8;

    if ((appid = mrp_allocz(sizeof(*appid)))) {
        appid->data = data;
        appid->map = mrp_htbl_create(&cfg);

        map_init(appid, app_dir);
    }

    return appid;
}


void mrp_resmgr_appid_destroy(mrp_resmgr_appid_t *appid)
{
    if (appid) {
        mrp_htbl_destroy(appid->map, TRUE);
        mrp_free(appid);
    }
}

const char *mrp_resmgr_appid_find_by_pid(mrp_resmgr_appid_t *appid,
                                         const char *pid)
{
    char exe[PATH_MAX];
    appid_map_t *entry;

    if (pid2exe(pid, exe, PATH_MAX) < 0)
        goto failed;

    if (!(entry = mrp_htbl_lookup(appid->map, exe)))
        goto failed;


    return entry->id;
    
 failed:
    return NULL;
}

static void map_init(mrp_resmgr_appid_t *appid, const char *dir_path)
{
    DIR *dir, *bindir;
    struct dirent *dirent, *binent;
    struct stat stat;
    const char *id;
    char subdir_path[PATH_MAX];
    char bindir_path[PATH_MAX];
    char exe[PATH_MAX];

    if (!(dir = opendir(dir_path))) {
        mrp_log_error("iiv-resource-manager: can't open directory %s: %s",
                      dir_path, strerror(errno));
        return;
    }

    while ((dirent = readdir(dir))) {
        id = dirent->d_name;

        snprintf(subdir_path, sizeof(subdir_path), "%s/%s", dir_path, id);

        if (lstat(subdir_path, &stat) < 0) {
            mrp_log_error("ivi-resource-manager: can't stat %s: %s",
                          subdir_path, strerror(errno));
            continue;
        }

        if (!S_ISDIR(stat.st_mode) || id[0] == '.')
            continue;

        snprintf(bindir_path, sizeof(bindir_path), "%s/bin", subdir_path);

        if (!(bindir = opendir(bindir_path))) {
            mrp_log_error("ivi-resource-manager: can't open directory %s: %s",
                          bindir_path, strerror(errno));
            continue;
        }

        while ((binent = readdir(bindir))) {
            snprintf(exe, sizeof(exe), "%s/%s", bindir_path, binent->d_name);

            if (lstat(exe, &stat) < 0) {
                mrp_log_error("ivi-resource-manager: can't stat %s: %s",
                              exe, strerror(errno));
                continue;
            }

            if (!S_ISREG(stat.st_mode) || !(stat.st_mode & 0111))
                continue;

            map_add_entry(appid, id, exe);
        }

        closedir(bindir);

    } /* while dirent */

    closedir(dir);
}

static void map_add_entry(mrp_resmgr_appid_t *appid,
                          const char *id,
                          const char *exe)
{
    appid_map_t *entry;

    if (!(entry = mrp_allocz(sizeof(*entry))) ||
        !(entry->id = mrp_strdup(id)) ||
        !(entry->exe = mrp_strdup(exe)))
    {
        mrp_log_error("ivi-resource-manager: can't allocate memory");
        return;
    }

    mrp_htbl_insert(appid->map, (void *)entry->exe, entry);

    mrp_log_info("ivi-resource-manager: map exe %s => appid %s",
                 entry->exe, entry->id);
}

static void map_free_entry(void *key, void *object)
{
    appid_map_t *me = (appid_map_t *)object;

    MRP_UNUSED(key);

    free((void *)me->id);
    free((void *)me->exe);
    free((void *)me);
}

static int pid2exe(const char *pid, char *buf, size_t len)
{
    FILE *f;
    char path[PATH_MAX];
    char *p;
    int st = -1;

    if (pid && buf && len > 0) {
        snprintf(path, sizeof(path), "/proc/%s/cmdline", pid);

        if ((f = fopen(path, "r"))) {
            if (fgets(buf, len-1, f)) {
                if ((p = strchr(buf, ' ')))
                    *p = '\0';
                else if ((p = strchr(buf, '\n')))
                    *p = '\0';
                st = 0;
            }
            fclose(f);
        }
    }

    if (st < 0)
        mrp_log_info("ivi-resource-manager: pid2exe(%s) failed", pid);
    else
        mrp_log_info("ivi-resource-manager: pid %s => exe %s", pid, buf);

    return st;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
