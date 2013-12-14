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
#include <ctype.h>
#include <errno.h>

#include <murphy/common.h>
#include <murphy/core/lua-bindings/murphy.h>
#include <murphy/core/lua-utils/error.h>
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>

#include <lualib.h>
#include <lauxlib.h>

#include "scripting-resource-manager.h"
#include "notifier.h"

#define SCREEN_EVENT_CLASS   MRP_LUA_CLASS_SIMPLE(screen_event)

typedef struct scripting_screen_event_s  scripting_screen_event_t;

struct scripting_screen_event_s {
    const char *eventid;
    const char *appid;
    int32_t surfaceid;
    int32_t layerid;
    const char *area;
};

static int  screen_event_create_from_lua(lua_State *);
static int  screen_event_getfield(lua_State *);
static int  screen_event_setfield(lua_State *);
static int  screen_event_stringify(lua_State *);
static void screen_event_destroy_from_lua(void *);

static scripting_screen_event_t *screen_event_check(lua_State *, int);

MRP_LUA_CLASS_DEF_SIMPLE (
    screen_event,                  /* class name */
    scripting_screen_event_t,      /* userdata type */
    screen_event_destroy_from_lua, /* userdata destructor */
    MRP_LUA_METHOD_LIST (          /* methods */
       MRP_LUA_METHOD_CONSTRUCTOR (screen_event_create_from_lua)
    ),
    MRP_LUA_METHOD_LIST (       /* overrides */
       MRP_LUA_OVERRIDE_CALL      (screen_event_create_from_lua)
       MRP_LUA_OVERRIDE_GETFIELD  (screen_event_getfield)
       MRP_LUA_OVERRIDE_SETFIELD  (screen_event_setfield)
       MRP_LUA_OVERRIDE_STRINGIFY (screen_event_stringify)
    )
);

void mrp_resmgr_scripting_notifier_init(lua_State *L)
{
    mrp_lua_create_object_class(L, SCREEN_EVENT_CLASS);
}


void *mrp_resmgr_scripting_screen_event_create_from_c(lua_State *L,
                                                     mrp_resmgr_event_t *event)
{
    scripting_screen_event_t *sev;
    const char *eventid;

    MRP_ASSERT(event, "invald argument");
    
    if (!L && !(L = mrp_lua_get_lua_state())) {
        mrp_log_error("system-controller: can't create scripting screen event:"
                      " LUA is not initialized");
        return NULL;
    }

    switch (event->eventid) {

    case MRP_RESMGR_EVENTID_GRANT:   eventid = "grant";   break;
    case MRP_RESMGR_EVENTID_REVOKE:  eventid = "revoke";  break;

    default:
        mrp_log_error("system-controller: can't create scripting screen event:"
                      " invalid event ID %d", event->eventid);
        return NULL;
    }

    sev = (scripting_screen_event_t *)mrp_lua_create_object(L,
                                                            SCREEN_EVENT_CLASS,
                                                            NULL, 0);
    if (!sev) {
        mrp_log_error("system-controller: can't create scripting screen event:"
                      " LUA object creation failed");
        return NULL;
    }

    sev->eventid   = eventid;
    sev->appid     = mrp_strdup(event->appid);
    sev->surfaceid = event->surfaceid;
    sev->layerid   = event->layerid;
    sev->area      = mrp_strdup(event->area);

    return sev;
}


static int screen_event_create_from_lua(lua_State *L)
{
    MRP_LUA_ENTER;

    luaL_error(L, "screen_event can't be created from LUA");

    MRP_LUA_LEAVE(1);
}

static int screen_event_getfield(lua_State *L)
{
    scripting_screen_event_t *sev;
    const char *fldnam;
    mrp_resmgr_scripting_field_t fld;

    MRP_LUA_ENTER;

    fld = mrp_resmgr_scripting_field_check(L, 2, &fldnam);
    lua_pop(L, 1);

    if (!(sev = screen_event_check(L, 1)))
        lua_pushnil(L);
    else {
        switch (fld) {
        case TYPE:      lua_pushstring(L, "screen");          break;
        case EVENT:     lua_pushstring(L, sev->eventid);      break;
        case APPID:     lua_pushstring(L, sev->appid);        break;
        case SURFACE:   lua_pushinteger(L, sev->surfaceid);   break;
        case LAYER:     lua_pushinteger(L, sev->layerid);     break;
        case AREA:      lua_pushstring(L, sev->area);         break;
        default:        lua_pushnil(L);                       break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int  screen_event_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    screen_event_check(L, 1);
    luaL_error(L, "screen_event objects are read-only");

    MRP_LUA_LEAVE(0);
}

static int  screen_event_stringify(lua_State *L)
{
    scripting_screen_event_t *sev;
    char *p, *e;
    char buf[4096];

    MRP_LUA_ENTER;

    if (!(sev = screen_event_check(L, 1)))
        lua_pushnil(L);
    else {
        e = (p = buf) + sizeof(buf);
        p += snprintf(p, e-p, "screen_event %s\n   appid: '%s'\n"
                      "   surface: %d\n   layer: %d\n   area: '%s'",
                      sev->eventid, sev->appid, sev->surfaceid, sev->layerid,
                      sev->area);

        lua_pushlstring(L, buf, p-buf);
    }

    MRP_LUA_LEAVE(1);
}

static void screen_event_destroy_from_lua(void *data)
{
    scripting_screen_event_t *sev = (scripting_screen_event_t *)data;

    MRP_LUA_ENTER;

    if (sev) {
        mrp_free((void *)sev->appid);
        mrp_free((void *)sev->area);
        mrp_free((void *)sev);
    }

    MRP_LUA_LEAVE_NOARG;
}


static scripting_screen_event_t *screen_event_check(lua_State *L, int idx)
{
    return (scripting_screen_event_t *)mrp_lua_check_object(L,
                                                            SCREEN_EVENT_CLASS,
                                                            idx);
}
