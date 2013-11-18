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

#ifndef __MURPHY_SYSTEM_CONTROLLER_DATA_TYPES_H__
#define __MURPHY_SYSTEM_CONTROLLER_DATA_TYPES_H__

typedef enum mrp_sysctl_scripting_field_e  mrp_sysctl_scripting_field_t;

enum mrp_sysctl_scripting_field_e {
    /*  2 */ ID = 1,
    /*  3 */ PID, TOP,
    /*  4 */ AREA, FLIP, HIDE, LEFT, MAKE, MASK, MOVE, NAME, NODE, SIZE, SHOW,
    /*  5 */ ALIGN, APPID, AUDIO, INDEX, INPUT, LAYER, MODEL, POS_X, POS_Y,
             RAISE, RIGHT, WIDTH,
    /*  6 */ ACTIVE, BOTTOM, HEIGHT, LAYERS, MAPPED, MIDDLE, OUTPUT, RESIZE,
             ROTATE, SCREEN,
    /*  7 */ CLASSES, DISPLAY, PIXEL_X, PIXEL_Y, SURFACE, VISIBLE,
    /*  8 */ POSITION, SUBPIXEL, VERTICAL,
    /*  9 */ LAYERTYPE, SHAREABLE,
    /* 10 */ ATTRIBUTES, HORIZONTAL, KEEPRATIO, PRIVILEGES,
    /* 11 */ AREA_CREATE, PIXEL_WIDTH,
    /* 12 */ PIXEL_HEIGHT, LAYER_UPDATE,
    /* 13 */ LAYER_REQUEST, OUTPUT_UPDATE, WINDOW_UPDATE,
    /* 14 */ OUTPUT_REQUEST, WINDOW_MANAGER, WINDOW_REQUEST,
};

#endif /* __MURPHY_SYSTEM_CONTROLLER_DATA_TYPES_H__ */
