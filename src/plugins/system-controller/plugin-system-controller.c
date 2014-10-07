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

#include <sys/stat.h>

#include <murphy/common/macros.h>
#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/transport.h>
#include <murphy/common/wsck-transport.h>
#include <murphy/common/json.h>
#include <murphy/core/plugin.h>
#include <murphy/core/lua-bindings/murphy.h>
#include <murphy/core/lua-bindings/lua-json.h>
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>

#include <tzplatform_config.h>

#include "system-controller.h"

#include "resource-manager/scripting-resource-manager.h"
#include "resource-client/scripting-resource-client.h"
#include "application/scripting-application.h"
#include "wayland/scripting-wayland.h"
#include "user/user.h"
#include "application-tracker/application-tracker.h"
#include "application-tracker/path-track.h"

#define DEFAULT_ADDRESS "wsck:127.0.0.1:18081/ico_syc_protocol"
#define DEFAULT_USER_CONFIG "/usr/apps/org.tizen.ico.system-controller/res/config/user.xml"
#define DEFAULT_WAYLAND_SOCKET "/run/user/5000/wayland-0"

/*
 * plugin argument ids/indices
 */

enum {
    ARG_ADDRESS,                         /* transport address to use */
    ARG_USER_CONFIG,                     /* user configuration file */
    ARG_USER_DIR,                        /* application stored data dir */
    ARG_WAYLAND_SOCKET,                  /* wayland socket that we wait for */
    ARG_AMD_SOCKET,                      /* wayland socket that we wait for */
};


/*
 * system-controller context
 */

typedef struct sysctl_lua_s sysctl_lua_t;

typedef struct {
    mrp_context_t    *ctx;                /* murphy context */
    mrp_transport_t  *lt;                 /* transport we listen on */
    const char       *addr;               /* address we listen on */
    const char       *user_config_file;   /* user manager configuration file */
    const char       *user_dir;           /* application stored data dir */
    char             *wayland_socket;     /* wayland socket path address */
    char             *amd_socket;         /* amd socket path address */
    mrp_path_track_t *wl_pt;              /* tracking the wayland socket path */
    mrp_path_track_t *amd_pt;             /* tracking the amd socket path */
    bool              wl_initialized;
    bool              amd_initialized;
    mrp_list_hook_t   clients;            /* connected clients */
    int               id;                 /* next client id */
    sysctl_lua_t     *scl;                /* singleton Lua object */
    lua_State        *L;                  /* murphy Lua state */
    struct {
        mrp_funcbridge_t *client;
        mrp_funcbridge_t *generic;
        mrp_funcbridge_t *window;
        mrp_funcbridge_t *input;
        mrp_funcbridge_t *user;
        mrp_funcbridge_t *resource;
        mrp_funcbridge_t *inputdev;
        mrp_funcbridge_t *notify;
    } handler;
} sysctl_t;


/*
 * a system-controller client
 */

typedef struct {
    int              id;                 /* client id */
    pid_t            pid;                /* client pid */
    char            *app;                /* client app id */
    sysctl_t        *sc;                 /* system controller context */
    mrp_transport_t *t;                  /* client transport */
    mrp_list_hook_t  hook;               /* to list of clients */
} client_t;


struct sysctl_lua_s {
    sysctl_t         *sc;                /* system controller */
};

typedef enum {
    SYSCTL_IDX_CLIENT = 1,
    SYSCTL_IDX_GENERIC,
    SYSCTL_IDX_WINDOW,
    SYSCTL_IDX_INPUT,
    SYSCTL_IDX_USER,
    SYSCTL_IDX_RESOURCE,
    SYSCTL_IDX_INPUTDEV,
    SYSCTL_IDX_NOTIFY
} sysctl_lua_index_t;


static int  sysctl_lua_create(lua_State *L);
static void sysctl_lua_destroy(void *data);
static int  sysctl_lua_getfield(lua_State *L);
static int  sysctl_lua_setfield(lua_State *L);
static int  sysctl_lua_stringify(lua_State *L);
static int  sysctl_lua_send(lua_State *L);

#define SYSCTL_LUA_CLASS MRP_LUA_CLASS(sysctl, lua)

MRP_LUA_METHOD_LIST_TABLE(sysctl_lua_methods,
                          MRP_LUA_METHOD_CONSTRUCTOR(sysctl_lua_create)
                          MRP_LUA_METHOD(send_message, sysctl_lua_send));

MRP_LUA_METHOD_LIST_TABLE(sysctl_lua_overrides,
                          MRP_LUA_OVERRIDE_CALL     (sysctl_lua_create)
                          MRP_LUA_OVERRIDE_GETFIELD (sysctl_lua_getfield)
                          MRP_LUA_OVERRIDE_SETFIELD (sysctl_lua_setfield)
                          MRP_LUA_OVERRIDE_STRINGIFY(sysctl_lua_stringify));

MRP_LUA_CLASS_DEF(sysctl, lua, sysctl_lua_t,
                  sysctl_lua_destroy, sysctl_lua_methods, sysctl_lua_overrides);




static sysctl_t *scptr;                  /* singleton native object */


static client_t *create_client(sysctl_t *sc, mrp_transport_t *lt)
{
    client_t *c;

    c = mrp_allocz(sizeof(*c));

    if (c != NULL) {
        mrp_list_init(&c->hook);

        c->sc = sc;
        c->t  = mrp_transport_accept(lt, c, MRP_TRANSPORT_REUSEADDR);

        if (c->t != NULL) {
            mrp_list_append(&sc->clients, &c->hook);
            c->id = sc->id++;

            return c;
        }

        mrp_free(c);
    }

    return NULL;
}


static void destroy_client(client_t *c)
{
    if (c != NULL) {
        mrp_list_delete(&c->hook);

        mrp_transport_disconnect(c->t);
        mrp_transport_destroy(c->t);

        mrp_free(c);
    }
}


static client_t *find_client_by_id(sysctl_t *sc, int id)
{
    mrp_list_hook_t *p, *n;
    client_t        *c;

    mrp_list_foreach(&sc->clients, p, n) {
        c = mrp_list_entry(p, typeof(*c), hook);

        if (c->id == id)
            return c;
    }

    return NULL;
}


static client_t *find_client_by_pid(sysctl_t *sc, pid_t pid)
{
    mrp_list_hook_t *p, *n;
    client_t        *c;

    mrp_list_foreach(&sc->clients, p, n) {
        c = mrp_list_entry(p, typeof(*c), hook);

        if (c->pid == pid)
            return c;
    }

    return NULL;
}


static client_t *find_client_by_app(sysctl_t *sc, const char *app)
{
    mrp_list_hook_t *p, *n;
    client_t        *c;

    mrp_list_foreach(&sc->clients, p, n) {
        c = mrp_list_entry(p, typeof(*c), hook);

        if (c->app != NULL && !strcmp(c->app, app))
            return c;
    }

    return NULL;
}


static void connection_evt(mrp_transport_t *lt, void *user_data)
{
    sysctl_t *sc = (sysctl_t *)user_data;
    client_t *c;

    c = create_client(sc, lt);

    if (c != NULL)
        mrp_log_info("Accepted system controller client connection.");
    else
        mrp_log_error("Failed to accept system controller client connection.");
}


static void disconnected_event(client_t *c)
{
    mrp_json_t            *req;
    int                    size;
    char                   buf[1024];
    mrp_funcbridge_t      *h;
    mrp_funcbridge_value_t args[4];
    mrp_funcbridge_value_t ret;
    char                   rt;

    /*
     * crate and emit a synthetic disconnection message if the client
     * disconnected
     */

    if (c->app)
        size = snprintf(buf, 1024,
                "{ \"command\" : 65535, \"appid\" : \"%s\", \"pid\" : %d }",
                c->app, c->pid);
    else
        size = snprintf(buf, 1024, "{ \"command\" : 65535, \"pid\" : %d }",
                c->pid);

    if (size < 0 || size >= 1024) {
        /* too long appid? */
        mrp_log_error("Could not create disconnect message");
        return;
    }

    req = mrp_json_string_to_object(buf, size);

    if (!req) {
        mrp_log_error("failed to parse json string '%s'", buf);
        return;
    }

    h = c->sc->handler.client;

    if (h != NULL) {
        args[0].pointer = c->sc->scl;
        args[1].integer = c->id;
        args[2].pointer = mrp_json_lua_wrap(c->sc->L, req);

        if (!mrp_funcbridge_call_from_c(c->sc->L, h, "odo", &args[0],
                                        &rt, &ret)) {
            mrp_log_error("Failed to dispatch client-connection (%s).",
                          ret.string ? ret.string : "<unknown error>");
            mrp_free((void *)ret.string);
        }
    }

    mrp_json_unref(req);
}


static void closed_evt(mrp_transport_t *t, int error, void *user_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(t);

    if (error != 0)
        mrp_log_error("System controller connection closed with error %d (%s).",
                      error, strerror(error));
    else
        mrp_log_info("System controller client #%d closed connection.", c->id);

    /* call the disconnection handler if it exists */
    disconnected_event(c);

    destroy_client(c);
}


static int send_message(client_t *c, mrp_json_t *msg)
{
    const char *s;

    s = mrp_json_object_to_string(msg);

    mrp_debug("sending system controller message to client #%d (%s):",
              c->id, c->app ? c->app : "<unknown>");
    mrp_debug("  %s", s);

    return mrp_transport_sendcustom(c->t, msg);
}


static const char *message_name(int code)
{
#define MAX_CLASS   0x08
#define MAX_TYPE    0x32
#define CLASS(code) (((code) & 0xf0000) >> 16)
#define TYPE(code)  (((code) & 0x0ffff))
    static char *names[MAX_CLASS + 1][MAX_TYPE + 1];
    static int   setup = 1;

    int   c = CLASS(code), t = TYPE(code);
    char *n;

    if (setup) {
        memset(names, 0, sizeof(names));

#define MAP(msg, code) names[CLASS(code)][TYPE(code)] = #msg;
        MAP(MSG_CMD_SEND_APPID          , 0x00000001);
        MAP(MSG_CMD_CREATE              , 0x00010001);
        MAP(MSG_CMD_DESTROY             , 0x00010002);
        MAP(MSG_CMD_SHOW                , 0x00010003);
        MAP(MSG_CMD_HIDE                , 0x00010004);
        MAP(MSG_CMD_MOVE                , 0x00010005);
        MAP(MSG_CMD_ANIMATION           , 0x00010006);
        MAP(MSG_CMD_CHANGE_ACTIVE       , 0x00010007);
        MAP(MSG_CMD_CHANGE_LAYER        , 0x00010008);
        MAP(MSG_CMD_CHANGE_ATTR         , 0x00010009);
        MAP(MSG_CMD_NAME                , 0x00010010);
        MAP(MSG_CMD_MAP_THUMB           , 0x00010020);
        MAP(MSG_CMD_UNMAP_THUMB         , 0x00010021);
        MAP(MSG_CMD_MAP_GET             , 0x00010022);
        MAP(MSG_CMD_SHOW_LAYER          , 0x00010030);
        MAP(MSG_CMD_HIDE_LAYER          , 0x00010031);
        MAP(MSG_CMD_CHANGE_LAYER_ATTR   , 0x00010032);
        MAP(MSG_CMD_ADD_INPUT           , 0x00020001);
        MAP(MSG_CMD_DEL_INPUT           , 0x00020002);
        MAP(MSG_CMD_SEND_INPUT          , 0x00020003);
        MAP(MSG_CMD_CHANGE_USER         , 0x00030001);
        MAP(MSG_CMD_GET_USERLIST        , 0x00030002);
        MAP(MSG_CMD_GET_LASTINFO        , 0x00030003);
        MAP(MSG_CMD_SET_LASTINFO        , 0x00030004);
        MAP(MSG_CMD_ACQUIRE_RES         , 0x00040001);
        MAP(MSG_CMD_RELEASE_RES         , 0x00040002);
        MAP(MSG_CMD_DEPRIVE_RES         , 0x00040003);
        MAP(MSG_CMD_WAITING_RES         , 0x00040004);
        MAP(MSG_CMD_REVERT_RES          , 0x00040005);
        MAP(MSG_CMD_WINDOW_ID_RES       , 0x00040006);
        MAP(MSG_CMD_SET_REGION          , 0x00050001);
        MAP(MSG_CMD_UNSET_REGION        , 0x00050002);
        MAP(MSG_CMD_CREATE_RES          , 0x00040011);
        MAP(MSG_CMD_DESTORY_RES         , 0x00040012);
        MAP(MSG_CMD_NOTIFY_CHANGED_STATE, 0x00060001);
#undef MAP

        setup = false;
    }

    if (c > MAX_CLASS || t > MAX_TYPE || (n = names[c][t]) == NULL)
        return "<unknown message>";
    else
        return n;
}


static void recv_evt(mrp_transport_t *t, void *data, void *user_data)
{
    client_t         *c   = (client_t *)user_data;
    sysctl_t         *sc  = c->sc;
    mrp_json_t       *req = (mrp_json_t *)data;
    int               cmd, type, pid;
    const char       *app;
    mrp_funcbridge_t *handlers[] = {
        [0] = sc->handler.client,
        [1] = sc->handler.window,
        [2] = sc->handler.input,
        [3] = sc->handler.user,
        [4] = sc->handler.resource,
        [5] = sc->handler.inputdev,
        [6] = sc->handler.notify,
    }, *h;
    mrp_funcbridge_value_t args[4];
    mrp_funcbridge_value_t ret;
    char                   rt;

    MRP_UNUSED(t);

    {
        const char *s = mrp_json_object_to_string(req);
        mrp_debug("system controller received message from client #%d:", c->id);
        mrp_debug("  %s", s);
    }

    /*
     * emit a connection message if this is the first message with an appid
     */

    if (mrp_json_get_integer(req, "pid", &pid))
        c->pid = pid;

    if (mrp_json_get_string(req, "appid", &app) && c->app == NULL) {
        mrp_free(c->app);
        c->app = mrp_strdup(app);

        mrp_debug("client #%d: pid %u, appid '%s'", c->id, c->pid,
                  c->app ? c->app : "");

        h = sc->handler.client;

        if (h != NULL) {
            args[0].pointer = sc->scl;
            args[1].integer = c->id;
            args[2].pointer = mrp_json_lua_wrap(sc->L, req);

            if (!mrp_funcbridge_call_from_c(sc->L, h, "odo", &args[0],
                                            &rt, &ret)) {
                mrp_log_error("Failed to dispatch client-connection (%s).",
                              ret.string ? ret.string : "<unknown error>");
                mrp_free((void *)ret.string);
            }
        }
    }


    if (!mrp_json_get_integer(req, "command", &cmd)) {
        h    = sc->handler.generic;
        type = 0;
    }
    else {
        type = (cmd & 0xffff0000) >> 16;

        if (0 <= type && type <= 6)
            h = handlers[type];
        else
            h = NULL;
    }

    if (h != NULL || (h = sc->handler.generic) != NULL) {
        args[0].pointer = sc->scl;
        args[1].integer = c->id;
        args[2].pointer = mrp_json_lua_wrap(sc->L, req);

        mrp_json_add_string(req, "MESSAGE", message_name(cmd));

        if (!mrp_funcbridge_call_from_c(sc->L, h, "odo", &args[0], &rt, &ret)) {
            mrp_log_error("Failed to dispatch system-controller message (%s).",
                          ret.string ? ret.string : "<unknown error>");
            mrp_free((void *)ret.string);
        }
    }
    else
        mrp_debug("No handler for system-controller message of type 0x%x.",
                  type);
}


static int transport_create(sysctl_t *sc)
{
    static mrp_transport_evt_t evt = {
        { .recvcustom     = recv_evt },
        { .recvcustomfrom = NULL     },
        .connection       = connection_evt,
        .closed           = closed_evt,
    };

    mrp_mainloop_t *ml = sc->ctx->ml;
    mrp_sockaddr_t  addr;
    socklen_t       len;
    const char     *type, *opt, *val;
    int             flags;

    len = mrp_transport_resolve(NULL, sc->addr, &addr, sizeof(addr), &type);

    if (len > 0) {
        flags    = MRP_TRANSPORT_REUSEADDR | MRP_TRANSPORT_MODE_CUSTOM;
        sc->lt = mrp_transport_create(ml, type, &evt, sc, flags);

        if (sc->lt != NULL) {
            if (mrp_transport_bind(sc->lt, &addr, len) &&
                mrp_transport_listen(sc->lt, 0)) {
                mrp_log_info("Listening on transport '%s'...", sc->addr);

                opt = MRP_WSCK_OPT_SENDMODE;
                val = MRP_WSCK_SENDMODE_TEXT;
                mrp_transport_setopt(sc->lt, opt, val);

                return TRUE;
            }

            mrp_transport_destroy(sc->lt);
            sc->lt = NULL;
        }
    }
    else
        mrp_log_error("Failed to resolve transport address '%s'.", sc->addr);

    return FALSE;
}


static void transport_destroy(sysctl_t *sc)
{
    mrp_transport_destroy(sc->lt);
    sc->lt = NULL;
}


static int sysctl_lua_create(lua_State *L)
{
    static sysctl_lua_t *scl = NULL;

    if (scl == NULL)
        scl = (sysctl_lua_t *)mrp_lua_create_object(L, SYSCTL_LUA_CLASS,
                                                    NULL, 0);
    scl->sc    = scptr;
    scptr->scl = scl;

    mrp_lua_push_object(L, scl);

    return 1;
}


static void sysctl_lua_destroy(void *data)
{
    MRP_UNUSED(data);
}


static sysctl_lua_t *sysctl_lua_check(lua_State *L, int idx)
{
    return (sysctl_lua_t *)mrp_lua_check_object(L, SYSCTL_LUA_CLASS, idx);
}


static int name_to_index(const char *name)
{
#define MAP(_name, _idx) if (!strcmp(name, _name)) return SYSCTL_IDX_##_idx;
    MAP("client_handler"  , CLIENT);
    MAP("generic_handler" , GENERIC);
    MAP("window_handler"  , WINDOW);
    MAP("input_handler"   , INPUT);
    MAP("user_handler"    , USER);
    MAP("resource_handler", RESOURCE);
    MAP("inputdev_handler", INPUTDEV);
    MAP("notify_handler"  , NOTIFY);
    return 0;
#undef MAP
}


static int sysctl_lua_getfield(lua_State *L)
{
    sysctl_lua_t       *scl = sysctl_lua_check(L, 1);
    sysctl_lua_index_t  idx = name_to_index(lua_tolstring(L, 2, NULL));

    lua_pop(L, 1);

    switch (idx) {
    case SYSCTL_IDX_CLIENT:
        mrp_funcbridge_push(L, scl->sc->handler.client);
        break;
    case SYSCTL_IDX_GENERIC:
        mrp_funcbridge_push(L, scl->sc->handler.generic);
        break;
    case SYSCTL_IDX_WINDOW:
        mrp_funcbridge_push(L, scl->sc->handler.window);
        break;
    case SYSCTL_IDX_INPUT:
        mrp_funcbridge_push(L, scl->sc->handler.input);
        break;
    case SYSCTL_IDX_USER:
        mrp_funcbridge_push(L, scl->sc->handler.user);
        break;
    case SYSCTL_IDX_RESOURCE:
        mrp_funcbridge_push(L, scl->sc->handler.resource);
        break;
    case SYSCTL_IDX_INPUTDEV:
        mrp_funcbridge_push(L, scl->sc->handler.inputdev);
        break;
    case SYSCTL_IDX_NOTIFY:
        mrp_funcbridge_push(L, scl->sc->handler.notify);
        break;
    default:
        lua_pushnil(L);
    }

    return 1;
}


static int sysctl_lua_setfield(lua_State *L)
{
    sysctl_lua_t        *scl  = sysctl_lua_check(L, 1);
    const char          *name = lua_tostring(L, 2);
    sysctl_lua_index_t   idx  = name_to_index(name);
    mrp_funcbridge_t   **hptr = NULL;

    switch (idx) {
    case SYSCTL_IDX_CLIENT:   hptr = &scl->sc->handler.client;   break;
    case SYSCTL_IDX_GENERIC:  hptr = &scl->sc->handler.generic;  break;
    case SYSCTL_IDX_WINDOW:   hptr = &scl->sc->handler.window;   break;
    case SYSCTL_IDX_INPUT:    hptr = &scl->sc->handler.input;    break;
    case SYSCTL_IDX_USER:     hptr = &scl->sc->handler.user;     break;
    case SYSCTL_IDX_RESOURCE: hptr = &scl->sc->handler.resource; break;
    case SYSCTL_IDX_INPUTDEV: hptr = &scl->sc->handler.inputdev; break;
    case SYSCTL_IDX_NOTIFY:   hptr = &scl->sc->handler.notify;   break;
    default:
        luaL_error(L, "unknown system-controller handler '%s'", name);
    }

    if (lua_type(L, -1) != LUA_TNIL)
        *hptr = mrp_funcbridge_create_luafunc(L, -1);
    else
        *hptr = NULL;

    mrp_debug("handler %s (#%d) of %p set to %p", name, idx, scl, *hptr);

    return 0;
}


static int sysctl_lua_stringify(lua_State *L)
{
    sysctl_lua_t *scl = sysctl_lua_check(L, 1);

    if (scl != NULL)
        lua_pushfstring(L, "<system-controller %p>", scl);
    else
        lua_pushstring(L, "<error (not a system-controller)>");

    return 1;
}


static int sysctl_lua_send(lua_State *L)
{
    sysctl_lua_t *scl = sysctl_lua_check(L, 1);
    client_t     *c;
    mrp_json_t   *msg;
    int           success;

    switch (lua_type(L, 2)) {
    case LUA_TSTRING:
        c = find_client_by_app(scl->sc, lua_tostring(L, 2));
        break;

    case LUA_TNUMBER:
        c = find_client_by_id(scl->sc, lua_tointeger(L, 2));
        break;

    default:
        return luaL_error(L, "invalid argument, client id or name expected");
    }

    msg = mrp_json_lua_get(L, 3);

    lua_pop(L, 3);

    if (c != NULL && msg != NULL) {
        mrp_debug("sending message %s to client #%d",
                  mrp_json_object_to_string(msg), c->id);
        success = send_message(c, msg);
    }
    else
        success = FALSE;

    mrp_json_unref(msg);
    lua_pushboolean(L, success);

    return 1;
}


static int sc_lua_get(lua_State *L)
{
    sysctl_lua_t *scl = mrp_lua_create_object(L, SYSCTL_LUA_CLASS, NULL, 0);

    scl->sc = scptr;

    mrp_lua_push_object(L, scl);

    return 1;
}


static bool init_user_scripting_if_ready(sysctl_t *sc)
{
    if (!sc->wl_initialized || !sc->amd_initialized) {
        /* we are still waiting for sockets */
        return TRUE;
    }

    if (!mrp_user_scripting_init(sc->L, sc->user_config_file,
            sc->user_dir, sc->ctx->ml)) {
        mrp_log_error("failed to initialize users!");
        return FALSE;
    }

    return TRUE;
}


static void wl_socket_cb(mrp_path_track_t *pt, enum mrp_path_track_event e,
        void *user_data)
{
    sysctl_t *sc = (sysctl_t *) user_data;

    switch (e) {
        case MRP_PATH_CREATED:
            mrp_log_info("wayland socket appeared");
            /* can stop listening for these events now */
            mrp_path_track_destroy(pt);
            sc->wl_pt = NULL;
            sc->wl_initialized = TRUE;

            init_user_scripting_if_ready(sc);
            break;
        case MRP_PATH_DELETED:
            /* we should not end here */
            mrp_log_info("wayland socket removed");
            /* TODO: could keep listening and survive a weston relaunch? */
            break;
        case MRP_PATH_INVALID:
            mrp_log_error("whole directory we were tracking is gone");
            mrp_path_track_destroy(pt);
            sc->wl_pt = NULL;
            break;
    }

    return;
}


static void amd_socket_cb(mrp_path_track_t *pt, enum mrp_path_track_event e,
        void *user_data)
{
    sysctl_t *sc = (sysctl_t *) user_data;

    switch (e) {
        case MRP_PATH_CREATED:
            mrp_log_info("amd socket appeared");
            /* can stop listening for these events now */
            mrp_path_track_destroy(pt);
            mrp_application_tracker_create();
            sc->amd_pt = NULL;
            sc->amd_initialized = TRUE;

            init_user_scripting_if_ready(sc);
            break;
        case MRP_PATH_DELETED:
            /* we should not end here */
            mrp_log_info("amd socket removed");
            break;
        case MRP_PATH_INVALID:
            mrp_log_error("whole directory we were tracking is gone");
            mrp_path_track_destroy(pt);
            sc->amd_pt = NULL;
            break;
    }

    return;
}


static void start_watching_sockets(mrp_timer_t *t, void *user_data)
{
    struct stat buf;
    sysctl_t *sc = (sysctl_t *) user_data;

    /* start waiting for the sockets to appear, delaying if need to */

    if (!sc->wl_initialized && !sc->wl_pt) {

        sc->wl_pt = mrp_path_track_create(sc->ctx->ml, sc->wayland_socket,
            wl_socket_cb, sc);

        if (!sc->wl_pt) {
            mrp_log_error("error: could not track wayland socket '%s'",
                sc->wayland_socket);
        }
        else if (stat(sc->wayland_socket, &buf) >= 0) {
            /* already there */
            mrp_path_track_destroy(sc->wl_pt);
            sc->wl_pt = NULL;
            sc->wl_initialized = TRUE;
        }
    }

    if (!sc->amd_initialized && !sc->amd_pt) {

        sc->amd_pt = mrp_path_track_create(sc->ctx->ml, sc->amd_socket,
            amd_socket_cb, sc);

        if (!sc->amd_pt) {
            mrp_log_error("error: could not track amd socket '%s'",
                    sc->amd_socket);
        }
        else if (stat(sc->amd_socket, &buf) >= 0) {
            mrp_path_track_destroy(sc->amd_pt);

            /* init the db table for application tracking */
            mrp_application_tracker_create();
            sc->amd_pt = NULL;
            sc->amd_initialized = TRUE;
        }
    }

    if (t)
        mrp_del_timer(t);

    if ((!sc->wl_pt && !sc->wl_initialized)
            || (!sc->amd_pt && !sc->amd_initialized)) {
        mrp_log_info("system-controller: delaying socket tracking");
        mrp_add_timer(sc->ctx->ml, 500, start_watching_sockets, sc);
    }
}


static int register_lua_bindings(sysctl_t *sc)
{
    static luaL_reg methods[] = {
        { "get_system_controller", sc_lua_get },
        { NULL, NULL }
    };
    static mrp_lua_bindings_t bindings = {
        .meta    = "murphy",
        .methods = methods,
    };

    if ((sc->L = mrp_lua_get_lua_state()) == NULL) {
        mrp_log_error("error: failed to get lua state");
        return FALSE;
    }

    mrp_resmgr_scripting_init(sc->L);
    mrp_resclnt_scripting_init(sc->L);
    mrp_application_scripting_init(sc->L);
    mrp_wayland_scripting_init(sc->L);

    /* delay user initialization until weston and amd both are running -- user
     * information is needed to launch HomeScreen with correct user */

    start_watching_sockets(NULL, sc);
    init_user_scripting_if_ready(sc);

    mrp_lua_create_object_class(sc->L, SYSCTL_LUA_CLASS);

    return mrp_lua_register_murphy_bindings(&bindings);
}


static int plugin_init(mrp_plugin_t *plugin)
{
    sysctl_t *sc;

    MRP_UNUSED(find_client_by_pid);

    sc = mrp_allocz(sizeof(*sc));

    if (sc != NULL) {
        char buf[512];
        int ret;

        mrp_list_init(&sc->clients);

        sc->id      = 1;
        sc->ctx     = plugin->ctx;
        sc->addr    = plugin->args[ARG_ADDRESS].str;
        sc->user_config_file = plugin->args[ARG_USER_CONFIG].str;
        sc->user_dir = plugin->args[ARG_USER_DIR].str;
        sc->wayland_socket = plugin->args[ARG_WAYLAND_SOCKET].str;
        sc->amd_socket = plugin->args[ARG_AMD_SOCKET].str;

        if (strcmp(sc->user_dir, "") == 0) {
            sc->user_dir = tzplatform_mkpath(TZ_USER_HOME, "ico");
        }

        if (strcmp(sc->wayland_socket, "") == 0) {
            /* generate good approximation of Wayland socket path */

            /* TODO: generate from environment variables -- this is just a
             * placeholder that replicates the previous systemd behavior */

            ret = snprintf(buf, sizeof(buf), "/run/user/%d/wayland-0",
                    getuid());

            if (ret < 0 || ret == sizeof(buf)) {
                mrp_log_error("error: snprintf");
                goto fail;
            }

            sc->wayland_socket = mrp_strdup(buf);
        }
        else
            sc->wayland_socket = mrp_strdup(sc->wayland_socket);

        if (strcmp(sc->amd_socket, "") == 0) {

            ret = snprintf(buf, sizeof(buf), "/run/user/%d/amd_agent",
                    getuid());

            if (ret < 0 || ret == sizeof(buf)) {
                mrp_log_error("error: snprintf");
                goto fail;
            }

            sc->amd_socket = mrp_strdup(buf);
        }
        else
            sc->amd_socket = mrp_strdup(sc->amd_socket);

        if (!sc->user_dir) {
            mrp_log_error("error: no user dir");
            goto fail;
        }

        if (!transport_create(sc)) {
            mrp_log_error("error: transport creation");
            goto fail;
        }

        if (!register_lua_bindings(sc)) {
            mrp_log_error("error: register lua bindings");
            goto fail;
        }

        scptr = sc;

        return TRUE;
    }

 fail:
    if (sc != NULL) {
        transport_destroy(sc);

        mrp_free(sc);
    }

    return FALSE;
}


static void plugin_exit(mrp_plugin_t *plugin)
{
    sysctl_t *sc = (sysctl_t *)plugin->data;

    scptr = NULL;

    mrp_application_tracker_destroy();

    if (sc->wl_pt)
        mrp_path_track_destroy(sc->wl_pt);

    if (sc->amd_pt)
        mrp_path_track_destroy(sc->amd_pt);

    transport_destroy(sc);

    mrp_free(sc->wayland_socket);
    mrp_free(sc->amd_socket);
    mrp_free(sc);
}



#define PLUGIN_DESCRIPTION "Murphy system controller plugin."
#define PLUGIN_HELP        "Murphy system controller plugin."
#define PLUGIN_AUTHORS     "Krisztian Litkey <kli@iki.fi>"
#define PLUGIN_VERSION     MRP_VERSION_INT(0, 0, 1)

static mrp_plugin_arg_t plugin_args[] = {
    MRP_PLUGIN_ARGIDX(ARG_ADDRESS, STRING, "address", DEFAULT_ADDRESS),
    MRP_PLUGIN_ARGIDX(ARG_USER_CONFIG, STRING, "user_config", DEFAULT_USER_CONFIG),
    MRP_PLUGIN_ARGIDX(ARG_USER_DIR, STRING, "user_dir", ""),
    MRP_PLUGIN_ARGIDX(ARG_WAYLAND_SOCKET, STRING, "wayland_socket", ""),
    MRP_PLUGIN_ARGIDX(ARG_AMD_SOCKET, STRING, "amd_socket", ""),
};

MURPHY_REGISTER_PLUGIN("system-controller",
                       PLUGIN_VERSION, PLUGIN_DESCRIPTION, PLUGIN_AUTHORS,
                       PLUGIN_HELP, MRP_SINGLETON, plugin_init, plugin_exit,
                       plugin_args, MRP_ARRAY_SIZE(plugin_args),
                       NULL, 0,
                       NULL, 0,
                       NULL);
