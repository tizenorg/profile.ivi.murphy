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

static void hs_check(mrp_timer_t *t, void *user_data);

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

static bool verify_path(const char *path)
{
    /* TODO: check if path contains illegal elements */

    MRP_UNUSED(path);

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


static int terminate_app(const aul_app_info *ai, void *user_data)
{
    int ret = 0;

    MRP_UNUSED(user_data);

    if (!ai)
        return 0;

    mrp_log_info("terminate %s", ai->appid ? ai->appid : "unknown application");

    /* keyboard, org.tizen.ico.homescreen, org.tizen.ico.statusbar */

    if (ai->appid && strcmp("keyboard", ai->appid) != 0) {
        ret = aul_terminate_pid(ai->pid);
        mrp_log_info("termination %s",
                ret < 0 ? "not successful" : "successful");
    }


    return 0;
}

static int iter_aul_app_info(const aul_app_info *ai, void *user_data)
{
    pid_t *hs_pid = (pid_t *) user_data;

    MRP_UNUSED(user_data);

    mrp_log_info("ai: pid %i, appid: %s, pkg_name %s", ai->pid, ai->appid,
            ai->pkg_name);

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

    bundle_add(b, "HS_PARAM_U", current_user->user_dir);
    bundle_add(b, "HS_PARAM_D", current_user->runningapp_path);
    bundle_add(b, "HS_PARAM_DD", ctx->defaultapps_path);
    bundle_add(b, "HS_PARAM_FLG", ctx->flag_file_path);

    mrp_log_info("launching new homescreen for %s, parameters: %s %s %s %s",
            current_user->name, current_user->user_dir,
            current_user->runningapp_path, ctx->defaultapps_path,
            ctx->flag_file_path);

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

#if 0
static int hs_dead_signal(int pid, void *user_data)
{
    user_manager_config_t *ctx = (user_manager_config_t *) user_data;

    mrp_log_info("dead app %i", pid);

    if (pid == ctx->current_hs_pid) {
        mrp_log_info("homescreen %i is dead", pid);
    }

    return 0;
}

static int hs_launch_signal(int pid, void *user_data)
{
    char buf[1024];
    user_manager_config_t *ctx = (user_manager_config_t *) user_data;

    mrp_log_info("launched app %i", pid);

    aul_app_get_appid_bypid(pid, buf, 1023);

    if (current_user && strcmp(buf, current_user->homescreen) == 0) {
        mrp_log_info("homescreen %i is alive", pid);
        /* ctx->current_hs_pid = pid; */
    }

    return 0;
}
#endif

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

            /* flag file on */

            create_flag_file(ctx);

            /* TODO: save current state meaning running programs */

            /* change the current user */

            current_user = config;

            /* get the current homescreen pid */

            if (ctx->current_hs_pid > 0) {
                /* terminate the current homescreen */
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

static bool set_lastinfo(lua_State *L, const char *appid, const char *lastinfo)
{
    char path[256];
    int appid_len = strlen(appid);
    int fd, base_len;
    char *base;

    MRP_UNUSED(L);

    /* FIXME: check that appid doesn't contain illegal characters */

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

    if (write(fd, lastinfo, sizeof(lastinfo)) < 0)
        mrp_log_error("failed to write to lastinfo file");

    close(fd);

    return TRUE;
}

static int lua_set_lastinfo(lua_State *L)
{
    const char *appid;
    const char *lastinfo;
    int narg;
    user_manager_t *ctx;

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

    appid = lua_tostring(L, -1);

    if (!appid)
        goto error;

    mrp_log_info("set_lastinfo appid: '%s', lastinfo: '%s'", appid, lastinfo);

    if (!set_lastinfo(L, appid, lastinfo))
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

    /* FIXME: check that appid doesn't contain illegal characters */

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

    if (stat(dir_path, &buf) == -1) {

        if (errno != ENOENT) {
            mrp_log_error("cannot access user data directory '%s'", dir_path);
            return NULL;
        }

        if (mkdir(dir_path, 0700) == -1) {
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

        if (mkdir(user_dir_path, 0700) == -1) {
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

static void hs_check(mrp_timer_t *t, void *user_data)
{
    user_manager_config_t *ctx = (user_manager_config_t *) user_data;
    /* mrp_pid_watch_t *w; */ /* TODO, when we get the capabilities */
    pid_t hs_pid = -1;

#if 0
    mrp_log_info("hs_check %i", ctx->homescreen_count);
#endif

    if (!current_user) {
        mrp_log_error("no current user for homescreen check");
        return;
    }

    /* is the homescreen already up? */

    hs_pid = get_hs_pid(current_user->homescreen);

    if (hs_pid > 0) {

        // current_hs_pid = hs_pid;
        ctx->current_hs_pid = hs_pid;
#if 0
        aul_listen_app_dead_signal(hs_dead_signal, ctx);
        aul_listen_app_launch_signal(hs_launch_signal, ctx);
#endif
        mrp_del_timer(t);
        ctx->t = NULL;
    }
    else if (ctx->homescreen_count >= 600) {
        mrp_log_error("didn't find homescreen in ten minutes, giving up");
        mrp_del_timer(t);
        ctx->t = NULL;
    }
    ctx->homescreen_count++;
}

static void user_manager_config_free(user_manager_config_t *ctx)
{
    mrp_free(ctx->defaultapps_path);
    mrp_free(ctx->flag_file_path);
    mrp_free(ctx->user_dir_base);
    mrp_free(ctx);
    return;
}

static bool user_init(mrp_mainloop_t *ml, const char *filename,
        const char *user_dir)
{
    char *default_user = NULL;
    current_user = NULL;
    mrp_list_hook_t *p, *n;
    int user_dir_len;
    user_manager_config_t *ctx;

    user_dir_len = strlen(user_dir);

    ctx = mrp_allocz(sizeof(user_manager_config_t));

    if (!ctx)
        return FALSE;

    ctx->ml = ml;

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
    memcpy(ctx->defaultapps_path+user_dir_len+1, "defaultApps.info", strlen("defaultApps.info"));

    /* load the user data (username and password pairs) from a file */

    mrp_log_info("user data parsing from %s", filename);

    mrp_list_init(&users);

    if (!parse_config_file(filename, &default_user, ctx)) {
        mrp_free(default_user);
        user_manager_config_free(ctx);
        mrp_log_error("parsing the user.xml file failed!");
        return FALSE;
    }

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

#if 0
        if (!current_user) {
            /* point to the first user by default */
            current_user = config;
        }
#endif

        if (default_user && strcmp(default_user, config->name) == 0)
            current_user = config;
    }

    mrp_free(default_user);

    /* create tmp dir for flag file */

    mkdir("/tmp/ico", 0700);

    ctx->flag_file_path = mrp_strdup("/tmp/ico/changeUser.flag");
    if (!ctx->flag_file_path) {
        user_manager_config_free(ctx);
        /* TODO: delete also the user list */
        return FALSE;
    }

    /* We need to know homescreen PID so that we can follow it. Query the
     * homescreen running status every second until it's up or ten minutes have
     * passed. */

    ctx->t = mrp_add_timer(ml, 1000, hs_check, ctx);

    return TRUE;
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

void mrp_user_scripting_init(lua_State *L, const char *config_file,
        const char *lastinfo_dir, mrp_mainloop_t *ml)
{
    MRP_UNUSED(L);

    user_init(ml, config_file, lastinfo_dir);
}

void mrp_user_scripting_deinit(lua_State *L)
{
    MRP_UNUSED(L);

    user_deinit();
}

MURPHY_REGISTER_LUA_BINDINGS(murphy, USER_MANAGER_CLASS,
                             { "UserManager", user_manager_create });