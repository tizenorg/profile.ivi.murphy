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

#include "scripting-wayland.h"
#include "output.h"
#include "area.h"
#include "window.h"
#include "layer.h"
#include "animation.h"



void mrp_wayland_scripting_init(lua_State *L)
{
    mrp_wayland_scripting_window_manager_init(L);
    mrp_wayland_scripting_animation_init(L);
    mrp_wayland_scripting_window_init(L);
    mrp_wayland_scripting_output_init(L);
    mrp_wayland_scripting_area_init(L);
    mrp_wayland_scripting_layer_init(L);

    mrp_wayland_scripting_input_manager_init(L);
    mrp_wayland_scripting_input_init(L);
    mrp_wayland_scripting_code_init(L);
}


char *mrp_wayland_scripting_canonical_name(const char *name,
                                           char *buf,
                                           size_t len)
{
    const char *q;
    char *p, *e, c;

    if (len < 2)
        return "";

    e = (p = buf) + (len - 1);
    q = name;

    if (isdigit(*q))
        *p++ = '_';

    while ((c = *q++) && p < e)
        *p++ = isalnum(c) ? c : '_';

    *p = 0;

    return buf;
}


int mrp_wayland_json_integer_copy(mrp_wayland_t *wl, void *uval,
                                  mrp_json_t *jval, int mask)
{
    const char *str;
    int32_t val;
    char *e;

    MRP_UNUSED(wl);

    if (mrp_json_is_type(jval, MRP_JSON_INTEGER)) {
        *(int32_t *)uval = mrp_json_integer_value(jval);
        return mask;
    }

    if (mrp_json_is_type(jval, MRP_JSON_STRING)) {
        str = mrp_json_string_value(jval);
        val = strtol(str, &e, 10);

        if (e > str && !*e) {
            *(int32_t *)uval = val;
            return mask;
        }
    }

    return 0;
}

int mrp_wayland_json_string_copy(mrp_wayland_t *wl, void *uval,
                                 mrp_json_t *jval, int mask)
{
    MRP_UNUSED(wl);

    if (mrp_json_is_type(jval, MRP_JSON_STRING)) {
        *(const char **)uval = mrp_json_string_value(jval);
        return mask;
    }

    return 0;
}

int mrp_wayland_json_boolean_copy(mrp_wayland_t *wl, void *uval,
                                  mrp_json_t *jval, int mask)
{
    const char *str;

    MRP_UNUSED(wl);

    if (mrp_json_is_type(jval, MRP_JSON_BOOLEAN)) {
        *(bool *)uval = mrp_json_boolean_value(jval) ? true : false;
        return mask;
    }

    if (mrp_json_is_type(jval, MRP_JSON_INTEGER)) {
        *(bool *)uval = mrp_json_integer_value(jval) ? true : false;
        return mask;
    }

    if (mrp_json_is_type(jval, MRP_JSON_STRING)) {
        str = mrp_json_string_value(jval);
        
        if (!strcmp(str,"yes") || !strcmp(str,"on") || !strcmp(str,"true")) {
            *(bool *)uval = true;
            return mask;
        }

        if (!strcmp(str,"no") || !strcmp(str,"off") || !strcmp(str,"false")) {
            *(bool *)uval = false;
            return mask;
        }
    }

    return 0;
}

int mrp_wayland_json_layer_copy(mrp_wayland_t *wl, void *uval,
                                mrp_json_t *jval, int mask)
{
    int32_t layerid;
    mrp_wayland_layer_t *layer;

    if (!mrp_json_is_type(jval, MRP_JSON_INTEGER))
        return 0;

    layerid = mrp_json_integer_value(jval);

    if (!(layer = mrp_wayland_layer_find_by_id(wl, layerid)))
        return 0;

    *(mrp_wayland_layer_t **)uval = layer;

    return mask;
}

int mrp_wayland_json_output_copy(mrp_wayland_t *wl, void *uval,
                                 mrp_json_t *jval, int mask)
{
    uint32_t index;
    mrp_wayland_output_t *out;

    if (!mrp_json_is_type(jval, MRP_JSON_INTEGER))
        return 0;

    index = mrp_json_integer_value(jval);

    if (!(out = mrp_wayland_output_find_by_index(wl, index)))
        return 0;

    *(mrp_wayland_output_t **)uval = out;

    return mask;
}

int mrp_wayland_json_area_copy(mrp_wayland_t *wl, void *uval,
                               mrp_json_t *jval, int mask)
{
    const char *fullname;
    mrp_wayland_area_t *area;

    if (!mrp_json_is_type(jval, MRP_JSON_STRING))
        return 0;

    fullname = mrp_json_string_value(jval);

    if (!(area = mrp_wayland_area_find(wl, fullname)))
        return 0;

    *(mrp_wayland_area_t **)uval = area;

    return mask;
}

int mrp_wayland_json_align_copy(mrp_wayland_t *wl, void *uval,
                                mrp_json_t *jval, int mask)
{
    const char *key;
    mrp_json_t *val;
    mrp_json_iter_t it;
    mrp_wayland_scripting_field_t fldid, valid;
    const char *valstr;
    mrp_wayland_area_align_t align;

    MRP_UNUSED(wl);

    if (!mrp_json_is_type(jval, MRP_JSON_ARRAY) &&
        !mrp_json_is_type(jval, MRP_JSON_INTEGER))
        return 0;

    if (mrp_json_is_type(jval, MRP_JSON_INTEGER))
        align = mrp_json_integer_value(jval);
    else {
        align = 0;
        mrp_json_foreach_member(jval, key,val, it) {
            if (!mrp_json_is_type(val, MRP_JSON_STRING))
                return 0;
            valstr = mrp_json_string_value(val);
            fldid = mrp_wayland_scripting_field_name_to_type(key, -1);
            valid = mrp_wayland_scripting_field_name_to_type(valstr, -1);
            switch (fldid) {
            case HORIZONTAL:
                switch (valid) {
                case LEFT:    align |= MRP_WAYLAND_AREA_ALIGN_LEFT;   break;
                case RIGHT:   align |= MRP_WAYLAND_AREA_ALIGN_RIGHT;  break;
                case MIDDLE:  align |= MRP_WAYLAND_AREA_ALIGN_MIDDLE; break;
                default:      /* invalid alignement */                return 0;
                }
                break;
            case VERTICAL:
                switch (valid) {
                case TOP:     align |= MRP_WAYLAND_AREA_ALIGN_TOP;    break;
                case BOTTOM:  align |= MRP_WAYLAND_AREA_ALIGN_BOTTOM; break;
                case MIDDLE:  align |= MRP_WAYLAND_AREA_ALIGN_MIDDLE; break;
                default:      /* invalid alignement */                return 0;
                }
                break;
            default:
                return 0;
            }
        }
    }

    *(mrp_wayland_area_align_t *)uval = align;

    return mask;
}


mrp_wayland_scripting_field_t
mrp_wayland_scripting_field_check(lua_State *L,int idx,const char **ret_fldnam)
{
    const char *fldnam;
    size_t fldnamlen;
    mrp_wayland_scripting_field_t fldtyp;

    if (!(fldnam = lua_tolstring(L, idx, &fldnamlen)))
        fldtyp = 0;
    else
        fldtyp = mrp_wayland_scripting_field_name_to_type(fldnam, fldnamlen);

    if (ret_fldnam)
        *ret_fldnam = fldnam;

    return fldtyp;
}

mrp_wayland_scripting_field_t
mrp_wayland_scripting_field_name_to_type(const char *name, ssize_t len)
{
    if (len < 0)
        len = strlen(name);

    switch (len) {

    case 2:
        if (!strcmp(name, "id"))
            return ID;
        break;

    case 3:
        switch (name[0]) {
        case 'm':
            if (!strcmp(name, "map"))
                return MAP;
            break;
        case 'p':
            if (!strcmp(name, "pid"))
                return PID;
            break;
        case 't':
            if (!strcmp(name, "top"))
                return TOP;
            break;
        default:
            break;
        }
        break;

    case 4:
        switch (name[0]) {
        case 'a':
            if (!strcmp(name, "area"))
                return AREA;
            break;
        case 'f':
            if (!strcmp(name, "flip"))
                return FLIP;
            break;
        case 'h':
            if (!strcmp(name, "hide"))
                return HIDE;
            break;
        case 'l':
            if (!strcmp(name, "left"))
                return LEFT;
            break;
        case 'm':
            if (!strcmp(name, "make"))
                return MAKE;
            if (!strcmp(name, "mask"))
                return MASK;
            if (!strcmp(name, "move"))
                return MOVE;
            break;
        case 'n':
            if (!strcmp(name, "name"))
                return NAME;
            if (!strcmp(name, "node"))
                return NODE;
            break;
        case 's':
            if (!strcmp(name, "show"))
                return SHOW;
            if (!strcmp(name, "size"))
                return SIZE;
            break;
        case 't':
            if (!strcmp(name, "time"))
                return TIME;
            if (!strcmp(name, "type"))
                return TYPE;
            break;
        default:
            break;
        }
        break;

    case 5:
        switch(name[0]) {
        case 'a':
            if (!strcmp(name, "align"))
                return ALIGN;
            if (!strcmp(name, "appid"))
                return APPID;
            break;
        case 'c':
            if (!strcmp(name, "codes"))
                return CODES;
            break;
        case 'i':
            if (!strcmp(name, "index"))
                return INDEX;
            if (!strcmp(name, "input"))
                return INPUT;
            break;
        case 'l':
            if (!strcmp(name, "layer"))
                return LAYER;
            break;
        case 'm':
            if (!strcmp(name, "model"))
                return MODEL;
            break;
        case 'p':
            if (!strcmp(name, "pos_x"))
                return POS_X;
            if (!strcmp(name, "pos_y"))
                return POS_Y;
            break;
        case 'r':
            if (!strcmp(name, "raise"))
                return RAISE;
            if (!strcmp(name, "right"))
                return RIGHT;
            break;
        case 's':
            if (!strcmp(name, "state"))
                return STATE;
            break;
        case 't':
            if (!strcmp(name, "touch"))
                return TOUCH;
            break;
        case 'w':
            if (!strcmp(name, "width"))
                return WIDTH;
            break;
        default:
            break;
        }
        break;

    case 6:
        switch (name[0]) {
        case 'a':
            if (!strcmp(name, "active"))
                return ACTIVE;
            break;
        case 'b':
            if (!strcmp(name, "bottom"))
                return BOTTOM;
            break;
        case 'f':
            if (!strcmp(name, "format"))
                return FORMAT;
            break;
        case 'h':
            if (!strcmp(name, "height"))
                return HEIGHT;
            if (!strcmp(name, "haptic"))
                return HAPTIC;
            break;
        case 'i':
            if (!strcmp(name, "inputs"))
                return INPUTS;
            break; 
        case 'l':
            if (!strcmp(name, "layers"))
                return LAYERS;
            break;
        case 'm':
            if (!strcmp(name, "mapped"))
                return MAPPED;
            if (!strcmp(name, "middle"))
                return MIDDLE;
            break;
        case 'o':
            if (!strcmp(name, "output"))
                return OUTPUT;
            break;
        case 'r':
            if (!strcmp(name, "resize"))
                return RESIZE;
            if (!strcmp(name, "rotate"))
                return ROTATE;
            break;
        case 's':
            if (!strcmp(name, "switch"))
                return SWITCH;
            if (!strcmp(name, "stride"))
                return STRIDE;
            break;
        case 't':
            if (!strcmp(name, "target"))
                return TARGET;
            break;
        default:
            break;
        }
        break;

    case 7:
        switch (name[0]) {
        case 'd':
            if (!strcmp(name, "display"))
                return DISPLAY;
            break;
        case 'k':
            if (!strcmp(name, "keycode"))
                return KEYCODE;
        case 'p':
            if (!strcmp(name, "pixel_x"))
                return PIXEL_X;
            if (!strcmp(name, "pixel_y"))
                return PIXEL_Y;
            if (!strcmp(name, "pointer"))
                return POINTER;
            break;
        case 's':
            if (!strcmp(name, "surface"))
                return SURFACE;
            break;
        case 'v':
            if (!strcmp(name, "visible"))
                return VISIBLE;
            break;
        default:
            break;
        }
        break;

    case 8:
        switch (name[0]) {
        case 'k':
            if (!strcmp(name, "keyboard"))
                return KEYBOARD;
            break;
        case 'p':
            if (!strcmp(name, "position"))
                return POSITION;
            break;
        case 's':
            if (!strcmp(name, "subpixel"))
                return SUBPIXEL;
            break;
        case 'v':
            if (!strcmp(name, "vertical"))
                return VERTICAL;
            break;
        default:
            break;
        }
        break;

    case 9:
        switch (name[0]) {
        case 'c':
            if (!strcmp(name, "connected"))
                return CONNECTED;
            break;
        case 'd':
            if (!strcmp(name, "device_id"))
                return DEVICE_ID;
            break;
        case 'l':
            if (!strcmp(name, "layertype"))
                return LAYERTYPE;
            break;
        case 'p':
            if (!strcmp(name, "permanent"))
                return PERMANENT;
            break;
        default:
            break;
        }
        break;

    case 10:
        switch (name[0]) {
        case 'h':
            if (!strcmp(name, "horizontal"))
                return HORIZONTAL;
            break;
        case 'k':
            if (!strcmp(name, "keep_ratio"))
                return KEEPRATIO;
            break;
        case 'o':
            if (!strcmp(name, "outputname"))
                return OUTPUTNAME;
            break;
        case 's':
            if (!strcmp(name, "send_input"))
                return SEND_INPUT;
            break;
        default:
            break;
        }
        break;

    case 11:
        switch (name[0]) {
        case 'a':
            if (!strcmp(name, "area_create"))
                return AREA_CREATE;
            break;
        case 'c':
            if (!strcmp(name, "code_update"))
                return CODE_UPDATE;
            break;
        case 'd':
            if (!strcmp(name, "device_name"))
                return DEVICE_NAME;
            break;
        case 'p':
            if (!strcmp(name, "pixel_width"))
                return PIXEL_WIDTH;
            break;
        default:
            break;
        }
        break;

    case 12:
        switch (name[0]) {
        case 'l':
            if (!strcmp(name, "layer_update"))
                return LAYER_UPDATE;
            break;
        case 'i':
            if (!strcmp(name, "input_update"))
                return INPUT_UPDATE;
            break;
        case 'p':
            if (!strcmp(name, "pixel_height"))
                return PIXEL_HEIGHT;
            break;
        default:
            break;
        }
        break;

    case 13:
        switch (name[0]) {
        case 'i':
            if (!strcmp(name, "input_request"))
                return INPUT_REQUEST;
            break;
        case 'l':
            if (!strcmp(name, "layer_request"))
                return LAYER_REQUEST;
            break;
        case 'o':
            if (!strcmp(name, "output_update"))
                return OUTPUT_UPDATE;
            break;
        case 'w':
            if (!strcmp(name, "window_update"))
                return WINDOW_UPDATE;
            break;
        default:
            break;
        }
        break;

    case 14:
        switch (name[0]) {
        case 'b':
            if (!strcmp(name, "buffer_request"))
                return BUFFER_REQUEST;
            break;
        case 'm':
            if (!strcmp(name, "manager_update"))
                return MANAGER_UPDATE;
            break;
        case 'o':
            if (!strcmp(name, "output_request"))
                return OUTPUT_REQUEST;
            break;
        case 'w':
            if (!strcmp(name, "window_manager"))
                return WINDOW_MANAGER;
            if (!strcmp(name, "window_request"))
                return WINDOW_REQUEST;
            break;
        default:
            break;
        }
        break;

    case 15:
        if (!strcmp(name, "manager_request"))
            return MANAGER_REQUEST;
        break;

    case 24:
        if (!strcmp(name, "passthrough_layer_update"))
            return PASSTHROUGH_LAYER_UPDATE;
        break;

    case 25:
        switch (name[12]) {
        case 'l':
            if (!strcmp(name, "passthrough_layer_request"))
                return PASSTHROUGH_LAYER_REQUEST;
            break;
        case 'w':
            if (!strcmp(name, "passthrough_window_update"))
                return PASSTHROUGH_WINDOW_UPDATE;
            break;
        default:
            break;
        }
        break;

    case 26:
        if (!strcmp(name, "passthrough_window_request"))
            return PASSTHROUGH_WINDOW_REQUEST;
        break;

    default:
        break;
    }

    return 0;
}

