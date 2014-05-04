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
#ifndef __MRP_PATTERN_GENERATOR_H__
#define __MRP_PATTERN_GENERATOR_H__

#include <stdint.h>
#include <stdbool.h>


typedef enum {
    allSources = -1,
    wrtApplication = 0,
    icoApplication,
    phoneSource,
    radio,
    microphone,
    navigator,
    SOURCE_MAX
} source_t;

typedef enum {
    allSinks = -1,
    noroute = 0,
    phoneSink,
    btHeadset,
    usbHeadset,
    speakers,
    wiredHeadset,
    voiceRecognition,
    SINK_MAX
} sink_t;

typedef enum {
    stop = 0,
    pause,
    play,
    STATE_MAX
} state_t;


#define CONNECTION_SOURCE(_conn) \
    (((_conn) >> 8) & 0xff)
#define CONNECTION_SINK(_conn) \
    ((_conn) & 0xff)

typedef uint16_t conn_t;

typedef uint8_t stamp_t;        /* SOURCE_MAX must be smaller than 8 */

#define ENTRY_CONNECTION(_entry) \
    ((conn_t)(((_entry) >> 16) & 0xffff))
#define ENTRY_STATE(_entry) \
    ((state_t)(_entry) & 0x0f)
#define ENTRY_STAMP(_entry) \
    ((stamp_t)(((_entry) >> 8) & 0xff))

typedef uint32_t entry_t;

typedef entry_t usecase_t [SOURCE_MAX];


void initialize_pattern_generator(const char *stemy, int max_active,
                                  source_t source);
void generate_patterns(int max_active, int source);
void generate_names(int max_active, int source);

size_t print_usecase(entry_t *usecase, char *buf, size_t len);

bool route_conflicts(entry_t entry, conn_t conn);

const char *get_source_name(source_t source);
const char *get_sink_name(sink_t sink);


#endif /* __MRP_PATTERN_GENERATOR_H__ */
