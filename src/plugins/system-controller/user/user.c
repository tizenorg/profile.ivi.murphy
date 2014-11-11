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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include <murphy/common.h>
#include <murphy/common/process.h>
#include <murphy/core/lua-bindings/murphy.h>
#include <murphy/core/lua-utils/error.h>
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>

#include <murphy-db/mqi.h>
#include <murphy-db/mql.h>

#include <lualib.h>
#include <lauxlib.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <aul/aul.h>
#include <bundle.h>

#include "user.h"

#define USER_MANAGER_CLASS MRP_LUA_CLASS(user_manager, lua)

/* TODO: associate the two structs with each other */

typedef struct {

} user_manager_t;

typedef struct {
    char *user_dir_base;
    pid_t current_hs_pid;
    char *defaultapps_path;
    char *flag_file_path;

    mrp_list_hook_t users;
    mrp_mainloop_t *ml;
    mrp_timer_t *t;
    int homescreen_count;

    user_manager_t *mgr;
} user_manager_config_t;

typedef struct {
    char *name;
    char *passwd;
    char *homescreen;
    char *user_dir;
    char *runningapp_path;

    user_manager_config_t *ctx;
    mrp_list_hook_t hook;
} user_config_t;

/* TODO: move current_user to ctx */
static user_config_t *current_user;

/* TODO: move users to ctx */
static mrp_list_hook_t users;

static int user_manager_create(lua_State *L);
static void user_manager_destroy(void *ctx);

static int lua_set_lastinfo(lua_State *L);
static int lua_get_lastinfo(lua_State *L);
static int lua_get_userlist(lua_State *L);
static int lua_change_user(lua_State *L);

#if 0
static void hs_check(mrp_timer_t *t, void *user_data);
#endif

MRP_LUA_METHOD_LIST_TABLE(user_manager_methods,
                          MRP_LUA_METHOD_CONSTRUCTOR(user_manager_create)
                          MRP_LUA_METHOD(getUserList, lua_get_userlist)
                          MRP_LUA_METHOD(setLastinfo, lua_set_lastinfo)
                          MRP_LUA_METHOD(getLastinfo, lua_get_lastinfo)
                          MRP_LUA_METHOD(changeUser, lua_change_user));

MRP_LUA_METHOD_LIST_TABLE(user_manager_overrides,
                          MRP_LUA_OVERRIDE_CALL     (user_manager_create));

MRP_LUA_DEFINE_CLASS(user_manager, lua, user_manager_t, user_manager_destroy,
        user_manager_methods, user_manager_overrides, NULL, NULL, NULL, NULL, NULL,
        MRP_LUA_CLASS_EXTENSIBLE);

static user_manager_t *user_manager_check(lua_State *L, int idx)
{
    return (user_manager_t *) mrp_lua_check_object(L, USER_MANAGER_CLASS, idx);
}

static void user_manager_destroy(void *ctx)
{
    MRP_UNUSED(ctx);
    mrp_log_info("user_manager_destroy");
}

static int user_manager_create(lua_State *L)
{
    user_manager_t *ctx = NULL;

    ctx = (user_manager_t *) mrp_lua_create_object(L, USER_MANAGER_CLASS, NULL,
            0);

    mrp_lua_push_object(L, ctx);

    return 1;
}

static bool mkdir_if_needed(const char *dir)
{
    mrp_debug("creating directory '%s'", dir);

    if (mkdir(dir, 0700) == -1) {
        struct stat buf;

        if (errno != EEXIST) {
            mrp_log_error("failed to create directory '%s'", dir);
            goto error;
        }

        /* there already is a directory or a file of that name */

        if (stat(dir, &buf) == -1) {
            mrp_log_error("failed to stat directory '%s'", dir);
            goto error;
        }

        if (!S_ISDIR(buf.st_mode)) {
            mrp_log_error("file '%s' exists and isn't a directory", dir);
            goto error;
        }
    }
    return TRUE;

error:
    return FALSE;
}

static bool mkdir_recursively_if_needed(const char *orig_path)
{
    char *save;
    char *token;
    char *path = mrp_strdup(orig_path);
    char *tokens[256];
    int n_tokens = 0;
    int success = FALSE;
    int i;

    /* only absolute paths allowed */
    if (!orig_path || !path || strlen(orig_path) == 0 || orig_path[0] != '/')
        goto end;

    memset(tokens, 0, sizeof(tokens));

    token = strtok_r(path, "/", &save);

    while (token) {
        if (strcmp(token, ".") == 0) {
            /* skip */
        }
        else if (strcmp(token, "..") == 0) {
            /* delete last one from the stack */
            if (n_tokens > 0) {
                tokens[--n_tokens] = NULL;
            }
            else {
                /* can't start with ".." */
                goto end;
            }
        }
        else {
            if (n_tokens == 256) {
                /* overflow */
                goto end;
            }
            tokens[n_tokens++] = token;
        }

        token = strtok_r(NULL, "/", &save);
    }

    for (i = 0; i < n_tokens; i++) {
        /* generate paths */
        char buf[1024];
        int ret;
        char *p = buf;
        int j;

        for (j = 0; j <= i; j++) {
            int remaining = 1024-(p-buf);
            ret = snprintf(p, remaining, "/%s", tokens[j]);
            if (ret < 0 || ret == remaining) {
                goto end;
            }
            p += ret;
        }

        if (!mkdir_if_needed(buf)) {
            goto end;
        }
    }

    success = TRUE;

end:
    mrp_free(path);

    return success;
}


static bool verify_appid(const char *appid)
{
    /* check if appid contains illegal elements, since it's being used in
       paths */

    /* allowed are [a-zA-Z0-9.-_] TODO: might be faster to do this with a
       lookup table */

    int i;
    int len;

    if (!appid)
        return FALSE;

    len = strlen(appid);

    if (len == 0)
        return FALSE;

    for (i = 0; i < len; i++) {
        char c = appid[i];

        if (c >= 'a' && c <= 'z')
            continue;

        if (c >= 'A' && c <= 'Z')
            continue;

        if (c >= '0' && c <= '9')
            continue;

        if (c == '.' || c == '-' || c == '_')
            continue;

        return FALSE;
    }

    return TRUE;
}

/* lua_get_userlist returns a list of users in the system. */

static int lua_get_userlist(lua_State *L)
{
    mrp_list_hook_t *p, *n;
    user_config_t *config;
    int i = 1, narg;
    user_manager_t *mgr;

    narg = lua_gettop(L);

    if (narg != 1)
        return luaL_error(L, "expecting no arguments");

    mgr = user_manager_check(L, 1);

    if (!mgr)
        return luaL_error(L, "no self pointer");

    /* make a user table and push it to the stack */

    lua_newtable(L);

    /* push all user strings to a table */

    mrp_list_foreach(&users, p, n) {
        config = mrp_list_entry(p, typeof(*config), hook);

        mrp_debug("read user '%s'", config->name);

        lua_pushinteger(L, i++);
        lua_pushstring(L, config->name);
        lua_settable(L, -3);
    }

    /* push current user to stack */

    if (current_user)
        lua_pushstring(L, current_user->name);
    else
        lua_pushnil(L);

    return 2;
}


static bool save_last_user(const char *user_dir, const char *user, int size)
{
    int ret;
    char last_user_file_buf[512];
    FILE *last_user_file = NULL;
    bool success = TRUE;

    if (!user)
        return FALSE;

    ret = snprintf(last_user_file_buf, sizeof(last_user_file_buf),
            "%s/lastuser.txt", user_dir);

    if (ret < 0 || ret == sizeof(last_user_file_buf)) {
        success = FALSE;
        goto end;
    }

    last_user_file = fopen(last_user_file_buf, "w");

    if (last_user_file) {
        ret = fwrite(user, size, 1, last_user_file);

        if (ret < 0) {
            success = FALSE;
            goto end;
        }

        mrp_log_info("system-controller: saved last user '%s'", user);
    }

end:
    if (last_user_file)
        fclose(last_user_file);

    return success;
}

static char *get_last_user(const char *user_dir)
{
    int ret;
    char *last_user = NULL;
    char last_user_buf[512];
    char last_user_file_buf[512];
    FILE *last_user_file = NULL;

    ret = snprintf(last_user_file_buf, sizeof(last_user_file_buf),
            "%s/lastuser.txt", user_dir);

    if (ret < 0 || ret == sizeof(last_user_file_buf)) {
        goto end;
    }

    last_user_file = fopen(last_user_file_buf, "r");

    if (last_user_file) {

        memset(last_user_buf, 0, sizeof(last_user_buf));

        ret = fread(last_user_buf, 1, sizeof(last_user_buf), last_user_file);

        if (ret < 0) {
            goto end;
        }
        else if (ret == 512) {
            /* too much data */
            goto end;
        }

        mrp_log_info("system-controller: last user '%s'", last_user_buf);

        last_user = mrp_strdup(last_user_buf);
    }

end:
    if (last_user_file)
        fclose(last_user_file);
    return last_user;
}

static int terminate_app(const aul_app_info *ai, void *user_data)
{
    int ret = 0;

    MRP_UNUSED(user_data);

    if (!ai)
        return 0;

    mrp_log_info("terminate %s", ai->appid ? ai->appid : "unknown application");

    /* org.tizen.ico.homescreen, org.tizen.ico.statusbar, others */

    ret = aul_terminate_pid(ai->pid);
    mrp_log_info("termination %s",
            ret < 0 ? "not successful" : "successful");

    return 0;
}

#if 0
static int iter_aul_app_info(const aul_app_info *ai, void *user_data)
{
    pid_t *hs_pid = (pid_t *) user_data;

    MRP_UNUSED(user_data);

    if (!ai || !ai->appid)
        return 0;

    mrp_log_info("ai: pid %i, appid: %s, pkg_name %s", ai->pid, ai->appid,
            ai->pkg_name ? ai->pkg_name : "NULL");

    if (current_user && strcmp(current_user->homescreen, ai->appid) == 0) {
        *hs_pid = ai->pid;
    }

    return 0;
}

static pid_t get_hs_pid(const char *homescreen)
{
    pid_t hs_pid = -1;

    if (!aul_app_is_running(homescreen))
        return -1;

    aul_app_get_running_app_info(iter_aul_app_info, &hs_pid);

    return hs_pid;
}
#endif

static bool launch_hs(user_manager_config_t *ctx)
{
    bundle *b = bundle_create();

    if (!b) {
        mrp_log_error("could not create bundle");
        return FALSE;
    }

    if (!current_user) {
        bundle_free(b);
        return FALSE;
    }

    bundle_add(b, "HS_PARAM_U", current_user->name);
    bundle_add(b, "HS_PARAM_D", current_user->runningapp_path);
    bundle_add(b, "HS_PARAM_DD", ctx->defaultapps_path);
    bundle_add(b, "HS_PARAM_FLG", ctx->flag_file_path);

    mrp_log_info("launching new homescreen, parameters: %s %s %s %s",
            current_user->name, current_user->runningapp_path,
            ctx->defaultapps_path, ctx->flag_file_path);

    ctx->current_hs_pid = aul_launch_app(current_user->homescreen, b);

    bundle_free(b);

    return TRUE;
}

static void create_flag_file(user_manager_config_t *ctx)
{
    FILE *f = fopen(ctx->flag_file_path, "w");
    if (f)
        fclose(f);
}

static void delete_flag_file(user_manager_config_t *ctx)
{
    unlink(ctx->flag_file_path);
}

static void launch_hs_deferred(mrp_timer_t *t, void *user_data)
{
    user_manager_config_t *ctx = (user_manager_config_t *) user_data;

    launch_hs(ctx);

    /* flag file off */

    delete_flag_file(ctx);

    mrp_del_timer(t);
}

/* change_user authenticates the user, destroys the current session and starts
 * a new session with the new user. */

static bool change_user(const char *user, const char *passwd)
{
    mrp_list_hook_t *p, *n;
    user_config_t *config;
    user_manager_config_t *ctx;

    if (strcmp(user, "") == 0 && strcmp(passwd, "") == 0) {
        /* this is a logout */

        if (!current_user) {
            mrp_log_error("trying to log out a non-existing user");
            return FALSE;
        }

        ctx = current_user->ctx;

        current_user = NULL;

        /* get the current homescreen pid */

        if (ctx->current_hs_pid > 0) {
            /* terminate the current homescreen and all other applications */
            mrp_log_info("terminating homescreen %i", ctx->current_hs_pid);
            aul_app_get_running_app_info(terminate_app, ctx);
            ctx->current_hs_pid = -1;
        }

        return TRUE;
    }

    mrp_list_foreach(&users, p, n) {
        config = mrp_list_entry(p, typeof(*config), hook);

        ctx = config->ctx;

        if (strcmp(user, config->name) == 0 &&
            strcmp(passwd, config->passwd) == 0) {

            mrp_timer_t *t;

            /* "authenticated" now */
            mrp_log_info("authenticated user %s", user);

            if (current_user && strcmp(user, current_user->name) == 0) {
                mrp_log_warning("user '%s' is already logged in", user);
                return TRUE;
            }

            /* save the user as last user */

            if (!save_last_user(ctx->user_dir_base, user, strlen(user))) {
                /* too bad */
                mrp_log_error("system-controller: failed to save last user");
            }

            /* flag file on */

            create_flag_file(ctx);

            /* TODO: save current state meaning running programs */

            /* change the current user */

            current_user = config;

            /* get the current homescreen pid */

            if (ctx->current_hs_pid > 0) {
                /* terminate the current homescreen and other applications */
                aul_app_get_running_app_info(terminate_app, ctx);
                ctx->current_hs_pid = -1;
            }

            /* launch new homescreen, wait the system controller amount :-) */

            /* TODO: should be done from dead_signal handler, but that doesn't
               appear to be working properly */

            t = mrp_add_timer(ctx->ml, 2000, launch_hs_deferred, ctx);

            if (!t)
                return FALSE;

            return TRUE;
        }
    }
    return FALSE;
}

static int lua_change_user(lua_State *L)
{
    const char *user;
    const char *passwd;
    int narg;
    user_manager_t *ctx;

    narg = lua_gettop(L);

    if (narg != 3)
        return luaL_error(L, "expecting two arguments");

    ctx = user_manager_check(L, 1);

    if (!ctx)
        return luaL_error(L, "no self pointer");

    if (!lua_isstring(L, -1) || !lua_isstring(L, -2))
        return luaL_error(L, "argument error");

    user = lua_tostring(L, -2);

    if (!user)
        return luaL_error(L, "string error");

    passwd = lua_tostring(L, -1);

    if (!passwd)
        return luaL_error(L, "string error");

    mrp_log_info("lua_change_user '%s'", user);

    if (change_user(user, passwd))
        lua_pushboolean(L, 1);
    else
        lua_pushboolean(L, 0);

    return 1;
}

/* set_lastinfo stores the lastinfo string to a file, whose name
   is derived from string appid. */

static bool set_lastinfo(lua_State *L, const char *appid, const char *lastinfo,
        size_t lastinfo_len)
{
    char path[256];
    int appid_len = strlen(appid);
    int fd, base_len;
    char *base;

    MRP_UNUSED(L);

    if (!verify_appid(appid))
        return FALSE;

    if (!current_user)
        return FALSE;

    base = current_user->user_dir;

    if (!base)
        return FALSE;

    base_len = strlen(base);

    if (base_len + 1 + appid_len >= 256)
        return FALSE;

    strcpy(path, base);
    path[base_len] = '/';
    strcpy(path+base_len+1, appid);
    path[base_len + 1 + appid_len] = '\0';

    mrp_log_info("writing lastinfo to '%s'\n", path);

    fd = open(path, O_WRONLY | O_CREAT, 0600);

    if (fd < 0) {
        mrp_log_error("failed to open lastinfo file for writing");
        return FALSE;
    }

    if (write(fd, lastinfo, lastinfo_len) < 0) {
        mrp_log_error("failed to write to lastinfo file");
        close(fd);
        return FALSE;
    }

    close(fd);

    return TRUE;
}

static int lua_set_lastinfo(lua_State *L)
{
    const char *appid;
    const char *lastinfo;
    int narg;
    user_manager_t *ctx;
    size_t lastinfo_len;

    narg = lua_gettop(L);

    if (narg != 3)
        return luaL_error(L, "expecting two arguments");

    ctx = user_manager_check(L, 1);

    if (!ctx)
        return luaL_error(L, "no self pointer");

    if (!lua_isstring(L, -1) || !lua_isstring(L, -2))
        goto error;

    lastinfo = lua_tostring(L, -2);

    if (!lastinfo)
        goto error;

    lastinfo_len = lua_strlen(L, -2);

    appid = lua_tostring(L, -1);

    if (!appid)
        goto error;

    mrp_log_info("set_lastinfo appid: '%s', lastinfo: '%s'", appid, lastinfo);

    if (!set_lastinfo(L, appid, lastinfo, lastinfo_len))
        goto error;

    return 1;

error:
    return luaL_error(L, "set_lastinfo error");}

/* get_lastinfo retrieves the lastinfo string from a file, whose
   name is derived from string appid. */

static bool get_lastinfo(lua_State *L, const char *appid)
{
    char path[256];
    char result[2048];

    int appid_len = strlen(appid);
    int fd, ret, base_len;
    char *base;

    MRP_UNUSED(L);

    if (!verify_appid(appid))
        return FALSE;

    if (!current_user)
        return FALSE;

    base = current_user->user_dir;

    if (!base)
        return FALSE;

    base_len = strlen(base);

    if (base_len + 1 + appid_len >= 256)
        return FALSE;

    strcpy(path, base);
    path[base_len] = '/';
    strcpy(path+base_len+1, appid);
    path[base_len + 1 + appid_len] = '\0';

    mrp_log_info("reading lastinfo from '%s'\n", path);

    fd = open(path, O_RDONLY);

    if (fd < 0) {
        mrp_log_warning("failed to read lastinfo from '%s'", path);
        return FALSE;
    }

    ret = read(fd, result, sizeof(result));

    if (ret >= 0 && ret != 2048) {
        result[ret] = '\0';
        lua_pushstring(L, result);
    }

    close(fd);

    return TRUE;
}

static int lua_get_lastinfo(lua_State *L)
{
    const char *appid;
    int narg;
    user_manager_t *ctx;

    narg = lua_gettop(L);

    if (narg != 2)
        return luaL_error(L, "expecting one argument");

    ctx = user_manager_check(L, 1);

    if (!ctx)
        return luaL_error(L, "no self pointer");

    if (!lua_isstring(L, -1))
        goto error;

    appid = lua_tostring(L, -1);

    if (!appid)
        goto error;

    mrp_log_info("get_lastinfo: appid: '%s'", appid);

    if (!get_lastinfo(L, appid))
        goto error;

    return 1;

error:
    return luaL_error(L, "get_lastinfo error");
}

static char *create_home_dir(const char *dir_path, const char *name)
{
    int dirpath_len = strlen(dir_path);
    int name_len = strlen(name);
    int user_dir_path_len = dirpath_len + 1 + name_len;
    char user_dir_path[user_dir_path_len + 1];
    struct stat buf;

    mrp_debug("create user home directory: %s, %s", dir_path, name);

    if (stat(dir_path, &buf) == -1) {

        if (errno != ENOENT) {
            mrp_log_error("cannot access user data directory '%s'", dir_path);
            return NULL;
        }

        if (!mkdir_recursively_if_needed(dir_path)) {
            mrp_log_error("could not create user data directory '%s'", dir_path);
            return NULL;
        }
    }

    strcpy(user_dir_path, dir_path);
    user_dir_path[dirpath_len] = '/';
    strcpy(user_dir_path + dirpath_len + 1, name);
    user_dir_path[user_dir_path_len] = '\0';

    if (stat(user_dir_path, &buf) == -1) {

        if (errno != ENOENT) {
            mrp_log_error("cannot access private user data directory '%s'",
                    user_dir_path);
            return NULL;
        }

        if (!mkdir_recursively_if_needed(user_dir_path)) {
            mrp_log_error("could not create private user data directory '%s'",
                    user_dir_path);
            return NULL;
        }
    }

    return mrp_strdup(user_dir_path);
}

static void delete_conf(user_config_t *conf)
{
    if (!conf)
        return;

    mrp_free(conf->homescreen);
    mrp_free(conf->name);
    mrp_free(conf->passwd);
    mrp_free(conf->user_dir);
    mrp_free(conf);
}

static bool parse_config_file(const char *filename, char **default_user_name,
        user_manager_config_t *ctx)
{
    xmlDocPtr doc = xmlParseFile(filename);
    xmlNodePtr root = NULL;
    int success = FALSE;

    if (!doc) {
        mrp_log_error("Error parsing document\n");
        exit(1);
    }

    root = xmlDocGetRootElement(doc);

    if (!root) {
        mrp_log_error("no root node in the document\n");
        xmlFreeDoc(doc);
        exit(1);
    }

    if (xmlStrcmp((xmlChar *) "userconfig", root->name) == 0) {
        xmlNodePtr section = root->xmlChildrenNode;

        /* can contain "user", "default", and "homescreens" */

        if (!section) {
            mrp_log_error("no section\n");
            goto end;
        }

        do {
            if (xmlStrcmp((xmlChar *) "users", section->name) == 0) {
                xmlNodePtr xusers = section->xmlChildrenNode;

                if (!xusers) {
                    mrp_log_error("no users\n");
                    goto end;
                }

                do {
                    if (xmlStrcmp((xmlChar *) "user", xusers->name) == 0) {
                        xmlNodePtr user_data = xusers->xmlChildrenNode;
                        user_config_t *conf = mrp_allocz(sizeof(user_config_t));

                        if (!user_data) {
                            mrp_log_error("no user_data\n");
                            delete_conf(conf);
                            goto end;
                        }

                        if (!conf)
                            goto end;

                        mrp_list_init(&conf->hook);
                        conf->ctx = ctx;

                        do {
                            if (xmlStrcmp((xmlChar *) "name", user_data->name) == 0) {
                                xmlChar *name;
                                name = xmlNodeListGetString(doc, user_data->xmlChildrenNode, 1);
                                if (!name) {
                                    delete_conf(conf);
                                    goto end;
                                }
                                conf->name = mrp_strdup((char *) name);
                                xmlFree(name);
                            }
                            else if (xmlStrcmp((xmlChar *) "passwd", user_data->name) == 0) {
                                xmlChar *passwd;
                                passwd = xmlNodeListGetString(doc, user_data->xmlChildrenNode, 1);
                                conf->passwd = passwd ? mrp_strdup((char *) passwd) : mrp_strdup("");
                                xmlFree(passwd);
                            }
                            else if (xmlStrcmp((xmlChar *) "hs", user_data->name) == 0) {
                                xmlChar *hs;
                                hs = xmlNodeListGetString(doc, user_data->xmlChildrenNode, 1);
                                if (!hs) {
                                    delete_conf(conf);
                                    goto end;
                                }
                                conf->homescreen = mrp_strdup((char *) hs);
                                xmlFree(hs);
                            }
                        } while ((user_data = user_data->next));

                        /* check if we have all the data we need */
                        if (!(conf->name && conf->passwd && conf->homescreen)) {
                            mrp_log_error("incomplete user data");
                            delete_conf(conf);
                            goto end;
                        }
                        mrp_list_append(&users, &conf->hook);
                    } /* if "user" */

                } while ((xusers = xusers->next));
            } /* if "users" */
            else if (xmlStrcmp((xmlChar *) "default", section->name) == 0) {
                xmlNodePtr default_user = section->xmlChildrenNode;

                if (!default_user) {
                    mrp_log_error("no default_user\n");
                    goto end;
                }

                do {
                    if (xmlStrcmp((xmlChar *) "user", default_user->name) == 0) {
                        xmlNodePtr user_data = default_user->xmlChildrenNode;

                        if (!user_data) {
                            mrp_log_error("no user_data\n");
                            goto end;
                        }
                        do {
                            if (xmlStrcmp((xmlChar *) "name", user_data->name) == 0) {
                                xmlChar *name;
                                name = xmlNodeListGetString(doc, user_data->xmlChildrenNode, 1);
                                if (!name)
                                    goto end;
                                *default_user_name = mrp_strdup((char *) name);
                                xmlFree(name);
                            }
                        } while ((user_data = user_data->next));
                    } /* if "user" */
                } while ((default_user = default_user->next));
            } /*if "default" */
        } while ((section = section->next));
    } /* if "user_config" */

    success = TRUE;
end:
    xmlFreeDoc(doc);

    return success;
}

static void user_manager_config_free(user_manager_config_t *ctx)
{
    mrp_free(ctx->defaultapps_path);
    mrp_free(ctx->flag_file_path);
    mrp_free(ctx->user_dir_base);
    mrp_free(ctx);
    return;
}

static void find_hs(user_manager_config_t *ctx, mql_result_t *select_r,
        mqi_event_type_t evtype)
{
    int i, n_rows;

    if (!select_r || select_r->type != mql_result_rows)
        return;

    n_rows = mql_result_rows_get_row_count(select_r);

    for (i = 0; i < n_rows; i++) {
        const char *appid;
        int pid;

        pid = mql_result_rows_get_integer(select_r, 0, i);
        appid = mql_result_rows_get_string(select_r, 1, i, NULL, 0);

        mrp_log_info("application %s (pid: %d) running",
            appid ? appid : "NULL", pid);

        if (!appid)
            continue;

        if (strcmp(appid, current_user->homescreen) == 0) {
            if (evtype == mqi_row_inserted) {
                ctx->current_hs_pid = pid;
                mrp_log_info("set current homescreen pid to %d", pid);
            }
            else if (evtype == mqi_row_deleted) {
                ctx->current_hs_pid = -1;
                mrp_log_info("reset current homescreen pid");
            }
            return;
        }
    }
}

static void application_event_cb(mql_result_t *result, void *user_data)
{
     user_manager_config_t *ctx = (user_manager_config_t *) user_data;

     if (!current_user || !current_user->homescreen)
        return;

    if (result->type == mql_result_event) {

        mqi_event_type_t evtype = mql_result_event_get_type(result);

        /* If the result value is asked as a string, this is what we get:

            event         table
            -------------------------------
            'row deleted'  aul_applications

                    pid appid
            ----------------------------------------------------------
                    369 org.tizen.ico.onscreen
        */

        find_hs(ctx, mql_result_event_get_changed_rows(result), evtype);
    }
}

static bool user_init(mrp_mainloop_t *ml, const char *filename,
        const char *user_dir)
{
    char *default_user = NULL;
    char *last_user = NULL;
    current_user = NULL;
    mrp_list_hook_t *p, *n;
    int user_dir_len, ret;
    user_manager_config_t *ctx;
    mql_result_t *r;
    mqi_handle_t tx;
    const char *trigger_s = "CREATE TRIGGER row_trigger"
            " ON ROWS IN aul_applications"
            " CALLBACK application_event_cb"
            " SELECT pid, appid";
    const char *select_s = "SELECT pid, appid FROM aul_applications";

    user_dir_len = strlen(user_dir);

    ctx = mrp_allocz(sizeof(user_manager_config_t));

    if (!ctx)
        return FALSE;

    ctx->ml = ml;
    ctx->current_hs_pid = -1; /* unknown still */

    ctx->user_dir_base = mrp_strdup(user_dir);
    ctx->defaultapps_path =
            mrp_allocz(user_dir_len + 1 + strlen("defaultApps.info") + 1);

    if (!ctx->user_dir_base || !ctx->defaultapps_path) {
        user_manager_config_free(ctx);
        return FALSE;
    }

    /* TODO: do a routine for generating and validating paths, since we appear
       to be creating a ton of them */

    memcpy(ctx->defaultapps_path, user_dir, user_dir_len);
    ctx->defaultapps_path[user_dir_len] = '/';
    memcpy(ctx->defaultapps_path+user_dir_len+1, "defaultApps.info",
            strlen("defaultApps.info"));

    /* load the user data (username and password pairs) from a file */

    mrp_log_info("user data parsing from %s", filename);

    mrp_list_init(&users);

    if (!parse_config_file(filename, &default_user, ctx)) {
        mrp_free(default_user);
        user_manager_config_free(ctx);
        mrp_log_error("parsing the user.xml file failed!");
        return FALSE;
    }

    /* read the last user file */

    last_user = get_last_user(user_dir);

    /* create user dirs and set the default user */

    mrp_list_foreach(&users, p, n) {
        user_config_t *config;
        config = mrp_list_entry(p, typeof(*config), hook);

        config->user_dir = create_home_dir(user_dir, config->name);

        if (!config->user_dir) {
            user_manager_config_free(ctx);
            return FALSE;
        }

        config->runningapp_path =
                mrp_allocz(strlen(config->user_dir) + 1 + strlen("runningapp.info") + 1);

        if (!config->runningapp_path) {
            user_manager_config_free(ctx);
            return FALSE;
        }

        memcpy(config->runningapp_path, config->user_dir, strlen(config->user_dir));
        config->runningapp_path[strlen(config->user_dir)] = '/';
        memcpy(config->runningapp_path+strlen(config->user_dir)+1,
                "runningapp.info", strlen("runningapp.info"));

        /* If there's a "last user", use that. If not, use "default user". */

        if (last_user && strcmp(last_user, config->name) == 0)
            current_user = config;

        if (!current_user && default_user &&
                strcmp(default_user, config->name) == 0)
            current_user = config;
    }

    if (current_user && current_user->name && !last_user) {
        /* if the last user wasn't set, now there will be one */
        save_last_user(user_dir, current_user->name,
                strlen(current_user->name));
    }

    mrp_free(last_user);
    mrp_free(default_user);

    /* create tmp dir for flag file */

    if (!mkdir_recursively_if_needed("/tmp/ico")) {
        goto error;
    }

    ctx->flag_file_path = mrp_strdup("/tmp/ico/changeUser.flag");
    if (!ctx->flag_file_path) {
        goto error;
    }

    ret = mql_register_callback("application_event_cb", mql_result_event,
            application_event_cb, ctx);

    if (ret < 0) {
        mrp_log_error("failed to register database trigger");
        goto error;
    }

    tx = mqi_begin_transaction();

    r = mql_exec_string(mql_result_string, trigger_s);

   if (!mql_result_is_success(r)) {
        mrp_log_error("db error: %s", mql_result_error_get_message(r));
    }

    mql_result_free(r);

    mqi_commit_transaction(tx);

    /* get the current list of running applications and see if homescreen
       is present */

    tx = mqi_begin_transaction();

    r = mql_exec_string(mql_result_rows, select_s);

    if (!mql_result_is_success(r)) {
        mrp_log_error("db error: %s", mql_result_error_get_message(r));

        /* find the home screen */
        find_hs(ctx, r, mqi_row_inserted);

        mql_result_free(r);
    }
    else {
        mql_result_free(r);
    }

    mqi_commit_transaction(tx);

    if (ctx->current_hs_pid <= 0) {
        create_flag_file(ctx);
        /* launch it! */
        launch_hs(ctx);
        delete_flag_file(ctx);
    }

    return TRUE;

error:
    /* TODO: delete also the user list */
    user_manager_config_free(ctx);
    return FALSE;
}

static bool user_deinit()
{
    mrp_list_hook_t *p, *n;

    /* TODO: stop ongoing timers */

    /* go through users and free user configs */

    mrp_list_foreach(&users, p, n) {
        user_config_t *config;
        config = mrp_list_entry(p, typeof(*config), hook);

        mrp_list_delete(&config->hook);

        delete_conf(config);
    }

    /* TODO: delete the ctx */

    return TRUE;
}

bool mrp_user_scripting_init(lua_State *L, const char *config_file,
        const char *lastinfo_dir, mrp_mainloop_t *ml)
{
    MRP_UNUSED(L);

    return user_init(ml, config_file, lastinfo_dir);
}

void mrp_user_scripting_deinit(lua_State *L)
{
    MRP_UNUSED(L);

    user_deinit();
}

MURPHY_REGISTER_LUA_BINDINGS(murphy, USER_MANAGER_CLASS,
                             { "UserManager", user_manager_create });
