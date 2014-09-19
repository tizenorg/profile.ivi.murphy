/*
 * Copyright (c) 2013, Intel Corporation
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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <murphy/common.h>

#include <murphy-db/mdb.h>
#include <murphy-db/mqi.h>
#include <murphy-db/mql.h>

#include <aul/aul.h>
#include <ail.h>

#define AUL_APPLICATION_TABLE_NAME "aul_applications"
#define DB_BUF_SIZE 1024

/* IMPORTANT: You need to have glib plugin loaded in order for this to work! */

static int aul_dead_signal(int pid, void *user_data)
{
    char buf[DB_BUF_SIZE];
    int buflen;
    mql_result_t *r;
    mqi_handle_t tx;

    MRP_UNUSED(user_data);

    mrp_log_info("tracker: dead app %i", pid);

    buflen = snprintf(buf, DB_BUF_SIZE, "DELETE FROM %s WHERE pid = +%i",
            AUL_APPLICATION_TABLE_NAME, pid);

    if (buflen <= 0 || buflen == DB_BUF_SIZE) {
        return 0;
    }

    tx = mqi_begin_transaction();

    mrp_log_info("tracker: '%s'", buf);

    r = mql_exec_string(mql_result_string, buf);

    mql_result_free(r);

    mqi_commit_transaction(tx);

    return 0;
}

static int add_app_to_db(const char *appid, pid_t pid, const char *category)
{
    char buf[DB_BUF_SIZE];
    int buflen;
    mql_result_t *r;
    mqi_handle_t tx;

    if (appid == NULL)
        appid = "<undefined>";

    if (category == NULL)
        category = "<undefined>";

    buflen = snprintf(buf, DB_BUF_SIZE, "INSERT INTO %s VALUES ('%s', %i, '%s')",
            AUL_APPLICATION_TABLE_NAME, appid, pid, category);

    if (buflen <= 0 || buflen == DB_BUF_SIZE) {
        return -1;
    }

    tx = mqi_begin_transaction();

    mrp_log_info("tracker: '%s'", buf);

    r = mql_exec_string(mql_result_string, buf);
    mql_result_free(r);

    mqi_commit_transaction(tx);

    return 0;
}

static ail_cb_ret_e handle_appinfo(const ail_appinfo_h appinfo, void *user_data)
{
    char *val = NULL;
    char **str = user_data;

    ail_appinfo_get_str(appinfo, AIL_PROP_CATEGORIES_STR, &val);

    mrp_debug("got category '%s'", val ? val : "NULL");

    if (val && strcmp(val, "(NULL)") != 0)
        *str = mrp_strdup(val);
    else
        *str = NULL;

    return AIL_CB_RET_CANCEL;
}

static char *get_category(const char *appid)
{
    ail_error_e e;
    char *value = NULL;
    ail_filter_h f;

    if (!appid)
        return NULL;

    e = ail_filter_new(&f);

    if (e != AIL_ERROR_OK)
        return NULL;

    e = ail_filter_add_str(f, AIL_PROP_X_SLP_APPID_STR, appid);
    if (e != AIL_ERROR_OK)
        goto end;

    e = ail_filter_list_appinfo_foreach(f, handle_appinfo, &value);
    if (e != AIL_ERROR_OK)
        goto end;

end:
    ail_filter_destroy(f);
    return value;
}

static int aul_launch_signal(int pid, void *user_data)
{
    char appid[512];
    char *category;

    MRP_UNUSED(user_data);

    memset(appid, 0, sizeof(appid));

    mrp_log_info("tracker: launched app %i", pid);

    if (aul_app_get_appid_bypid(pid, appid, 511) < 0) {
        appid[0] = '\0';
    }

    category = get_category(appid);

    add_app_to_db(appid, pid, category);

    mrp_free(category);

    return 0;
}

static int aul_iter_app_info(const aul_app_info *ai, void *user_data)
{
    char *category;

    MRP_UNUSED(user_data);

    mrp_log_info("tracker: app info pid %i, appid: %s, pkg_name %s", ai->pid,
            ai->appid ? ai->appid : "NULL", ai->pkg_name);

    category = get_category(ai->appid);

    add_app_to_db(ai->appid, ai->pid, category);

    mrp_free(category);

    return 0;
}

static int aul_handler(aul_type type, bundle *b, void *user_data)
{
    MRP_UNUSED(type);
    MRP_UNUSED(b);
    MRP_UNUSED(user_data);

    mrp_log_info("tracker: aul_handler");
    return 0;
}

int mrp_application_tracker_create()
{
    mqi_handle_t table;
    mqi_column_def_t defs[4];

    /* init the database table */

    defs[0].name = "appid";
    defs[0].type = mqi_varchar;
    defs[0].length = 64;
    defs[0].flags = 0;

    defs[1].name = "pid";
    defs[1].type = mqi_integer;
    defs[1].flags = 0;

    defs[2].name = "category";
    defs[2].type = mqi_varchar;
    defs[2].length = 64;
    defs[2].flags = 0;

    memset(&defs[3], 0, sizeof(defs[3]));

    table = MQI_CREATE_TABLE(AUL_APPLICATION_TABLE_NAME, MQI_TEMPORARY,
            defs, NULL);

    if (!table)
        return -1;

    /* set the application launch/death callbacks */

    if (aul_launch_init(aul_handler, NULL) < 0) {
        mrp_log_error("failed to init AUL");
        return -1;
    }

    if (aul_listen_app_dead_signal(aul_dead_signal, NULL) < 0) {
        mrp_log_error("failed to listen to application death signals");
        return -1;
    }

    if (aul_listen_app_launch_signal(aul_launch_signal, NULL) < 0) {
        mrp_log_error("failed to listen to application launch signals");
        return -1;
    }

    /* read the existing applications to the database */

    if (aul_app_get_running_app_info(aul_iter_app_info, NULL) < 0) {
        mrp_log_error("failed to query running applications");
        return -1;
    }

    return 0;
}

int mrp_application_tracker_destroy()
{
    /* TODO: delete the table, deinit AUL */
    return 0;
}
