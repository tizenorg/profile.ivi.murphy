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
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <ctype.h>
#include <errno.h>

#include "pattern-generator.h"
#include "decision-maker.h"

#define CONNECTION(_source, _sink)  \
    (((conn_t)((_source) & 0xff) << 8) | ((conn_t)((_sink) & 0xff)))

#define INVALID_ENTRY (~(entry_t)0)
#define ENTRY(_source, _sink, _state, _stamp) \
    (((entry_t)(CONNECTION(_source, _sink) << 16)) |  \
     (((entry_t)(_stamp) & 0xff) << 8) | \
     (((entry_t)(_state)) & 0x0f))


typedef struct {
        sink_t sink;
        conn_t conflict[SOURCE_MAX + 1];
} conn_def_t [SINK_MAX];

typedef struct {
    int bits;
    int8_t bitseq[SOURCE_MAX];
} stamp_pattern_t;

typedef struct {
    int pattern;
    int active;
    uint32_t sequence;
} index_t;


static const char *source_names[SOURCE_MAX] = {
    "wrtApplication",
    "icoApplication",
    "phone",
    "radio",
    "microphone",
    "navigator",
};

static const char *sink_names[SINK_MAX] = {
    "noroute",
    "phone",
    "btHeadset",
    "usbHeadset",
    "speakers",
    "wiredHeadset",
    "voiceRecognition",
};

static const char *state_names[STATE_MAX] = {
    "stop",
    "pause",
    "play",
};


static conn_def_t conn_defs[SOURCE_MAX] = {
    [wrtApplication]       = {
        [noroute]          = { noroute,           {} },
        [btHeadset]        = { btHeadset,         {} },
        [usbHeadset]       = { usbHeadset,        {} },
        [speakers]         = { speakers,          { CONNECTION(wrtApplication, wiredHeadset) }},
        [wiredHeadset]     = { wiredHeadset,      { CONNECTION(wrtApplication, speakers)     }},
    },
    [icoApplication] = {
        [noroute]          = { noroute,           {} },
        [btHeadset]        = { btHeadset,         {} },
        [usbHeadset]       = { usbHeadset,        {} },
        [speakers]         = { speakers,          { CONNECTION(icoApplication, wiredHeadset),
                                                    CONNECTION(phoneSource, speakers),
                                                    CONNECTION(phoneSource, wiredHeadset) }},
        [wiredHeadset]     = { wiredHeadset,      { CONNECTION(icoApplication, speakers),
                                                    CONNECTION(phoneSource, speakers),
                                                    CONNECTION(phoneSource, wiredHeadset) }},
    },
    [phoneSource]    = {
        [noroute]          = { noroute,           {} },
        [btHeadset]        = { btHeadset,         { CONNECTION(phoneSource, usbHeadset),
                                                    CONNECTION(phoneSource, speakers),
                                                    CONNECTION(phoneSource, wiredHeadset) }},
        [usbHeadset]       = { usbHeadset,        { CONNECTION(phoneSource, btHeadset),
                                                    CONNECTION(phoneSource, speakers),
                                                    CONNECTION(phoneSource, wiredHeadset) }},
        [speakers]         = { speakers,          { CONNECTION(phoneSource, btHeadset),
                                                    CONNECTION(phoneSource, usbHeadset),
                                                    CONNECTION(icoApplication, speakers),
                                                    CONNECTION(icoApplication, wiredHeadset),
                                                    CONNECTION(phoneSource, wiredHeadset) }},
        [wiredHeadset]     = { wiredHeadset,      { CONNECTION(phoneSource, btHeadset),
                                                    CONNECTION(phoneSource, usbHeadset),
                                                    CONNECTION(icoApplication, speakers),
                                                    CONNECTION(icoApplication, wiredHeadset),
                                                    CONNECTION(phoneSource, speakers) }},
    },
    [radio]          = {
        [noroute]          = { noroute,          {} },
        [btHeadset]        = { btHeadset,        { CONNECTION(radio, usbHeadset) }},
        [usbHeadset]       = { usbHeadset,       { CONNECTION(radio, btHeadset) }},
        [speakers]         = { speakers,         {} },
        [wiredHeadset]     = { wiredHeadset,     {} },
    },
    [microphone]     = {
        [noroute]          = { noroute,          {} },
        [phoneSink]        = { phoneSink,        { CONNECTION(microphone, voiceRecognition) }},
        [voiceRecognition] = { voiceRecognition, { CONNECTION(microphone, phoneSink) }}
    },
    [navigator]      = {
        [noroute]          = { noroute,          {} },
        [speakers]         = { speakers,         { CONNECTION(navigator, wiredHeadset) }},
        [wiredHeadset]     = { wiredHeadset,     { CONNECTION(navigator, speakers) }}
    },
};

static uint32_t factorial[9] = { 0, 1, 2, 6, 24, 120, 720, 5040, 40320 };

static int npattern[SOURCE_MAX+1];
static stamp_pattern_t patterns[SOURCE_MAX+1][1024];
static int nsequence[SOURCE_MAX+1];
static uint8_t *sequences[SOURCE_MAX+1];
static int nindex[SOURCE_MAX+1];
static index_t indices[SOURCE_MAX+1][100000];

#define DECISION_SPACE_DIMENSION (1 << (SOURCE_MAX * CONN_STATE_BITS))
static bool decisions[DECISION_SPACE_DIMENSION];
static uint32_t pattern_counter;
static uint32_t decision_counter;

static FILE *data;
static FILE *names;


static sink_t next_sink(source_t source, sink_t sink)
{
    while (++sink < SINK_MAX) {
        if (conn_defs[source][sink].sink == sink) {
            break;
        }
    }
    return sink;
}

static bool next_entry(entry_t *usecase, source_t source, stamp_t stamp, entry_t *cursor)
{
    entry_t entry, new_entry, new_cursor;
    conn_t conn;
    sink_t sink;
    state_t state;

    if (source < 0 || source >= SOURCE_MAX)
        return false;

    if ((entry = *cursor) == INVALID_ENTRY)
        return false;

    if (stamp == 0) {
        new_entry = ENTRY(source, noroute, stop, 0);
        new_cursor = INVALID_ENTRY;
    }
    else {
        conn = ENTRY_CONNECTION(entry);
        state = ENTRY_STATE(entry);
        sink = CONNECTION_SINK(conn);

        for (;;) {
            if (state >= STATE_MAX) {
                *cursor = INVALID_ENTRY;
                return false;
            }

            if (sink >= SINK_MAX) {
                state++;
                sink = 0;
                continue;
            }

            if (sink == noroute && state != stop) {
                sink = next_sink(source, sink);
                continue;
            }

            new_entry = ENTRY(source, sink, state, stamp);

            sink = next_sink(source, sink);
            new_cursor = ENTRY(source, sink, state, stamp);

            break;
        }
    }

    *cursor = new_cursor;
    usecase[source] = new_entry;

    return true;
}


static void populate_active_patterns(void)
{
#define BIT(b) ((int)1 << (b))

    stamp_pattern_t *p;
    int n;
    int i,j,k;
    int cnt;
    int8_t bitseq[SOURCE_MAX];

    for (i = 1;  i < SOURCE_MAX;  i++) {
        for (n = 0, k = 0;  n < BIT(SOURCE_MAX); n++) {
            memset(bitseq, -1, sizeof(bitseq));
            for (j = 0, cnt = 0;  j < SOURCE_MAX;  j++) {
                if (n & BIT(j))
                    bitseq[j] = cnt++;
            }
            if (cnt <= i) {
                p = &patterns[i][k++];
                p->bits = cnt;
                memcpy(p->bitseq, bitseq, sizeof(bitseq));
            }
        }
        npattern[i] = k;
    }

#undef BIT
}

static void populate_sequences(void)
{
    static uint32_t max[9] = { 0, 1, 4, 27, 256, 3125, 46656, 823543, 16777216 };
    static uint8_t seqs[(SOURCE_MAX +1) * (40320 *SOURCE_MAX)];

    uint8_t *sp;
    uint32_t i, j, k, n, v, s;
    bool valid;

    nsequence[0] = 1;
    sequences[0] = seqs;

    for (i = 1, sp = seqs + 1;  i <= SOURCE_MAX;  i++) {
        sequences[i] = sp;

        for (n = 0;  n < max[i];  n++) {
            for (j = 0, v = n, valid = true;    j < i && valid;   j++, v /= i) {
                sp[j] = s = (v % i) + 1;
                for (k = 0;  k < j;  k++) {
                    if (s == sp[k]) {
                        valid = false;
                        break;
                    }
                }
            }
            if (valid)
                sp += (SOURCE_MAX + 1);
        }

        nsequence[i] = (sp - sequences[i]) / (SOURCE_MAX + 1);
    }
}

static uint32_t seqno_max(int max_active)
{
    if (max_active > 8 || max_active > SOURCE_MAX)
        return 0;

   return nindex[max_active];
}


static void populate_indices(void)
{
    stamp_pattern_t *pattern;
    int i,j;
    int active;
    uint32_t cnt, combinations, k;
    index_t *idx;

    for (i = 1;  i <= SOURCE_MAX;  i++) {
        idx = &indices[i][0];
        for (cnt = 0, j = 0;   j < npattern[i];   j++) {
            pattern = &patterns[i][j];
            active = pattern->bits;
            combinations = factorial[pattern->bits];
            cnt += combinations;
            for (k = 0;  k < combinations; k++, idx++) {
                idx->pattern = j;
                idx->active = active;
                idx->sequence = k;
            }
        }
        nindex[i] = cnt;
    }
}


static stamp_t source_stamp(uint32_t seqno, source_t source, int max_active)
{
    stamp_pattern_t *pattern;
    int active;
    int8_t *bitseq;
    index_t *idx;
    uint8_t *seq;

    if (max_active <= 0)
        return 0;

    if (max_active > SOURCE_MAX)
        max_active = SOURCE_MAX;

    if (seqno >= seqno_max(max_active))
        return 0;

    idx = &indices[max_active][seqno];

    pattern = &patterns[max_active][idx->pattern];
    active  = pattern->bits;
    bitseq  = pattern->bitseq;

    if (bitseq[source] < 0)
        return 0;

    seq = sequences[active] + (idx->sequence * (SOURCE_MAX + 1));

    return seq[bitseq[source]];
}

static bool valid_usecase(entry_t *usecase)
{
    entry_t entry;
    stamp_t stamp;
    state_t state;
    conn_t conn;
    sink_t sink;
    int i;

    for (i = 0; i < SOURCE_MAX;  i++) {
        entry = usecase[i];
        stamp = ENTRY_STAMP(entry);
        state = ENTRY_STATE(entry);
        conn  = ENTRY_CONNECTION(entry);
        sink  = CONNECTION_SINK(conn);

        if (!stamp && (sink != noroute || state != stop))
            return false;
    }

    return true;
}

static void generate_usecases(int max_active,
                              source_t source,
                              entry_t *usecase,
                              entry_t *cursor,
                              stamp_t *stamp,
                              uint32_t seqno,
                              source_t idx)
{
    decision_t decision;
    char ubuf[256];
    char dbuf[64];

    if (idx == SOURCE_MAX) {
        if (valid_usecase(usecase)) {
            decision = make_decision(usecase, source, max_active);

            print_usecase(usecase, ubuf,sizeof(ubuf));
            print_decision(decision, source, dbuf,sizeof(dbuf));

            fprintf(data, "%s %s\n", ubuf, dbuf);

            decisions[decision] = true;
            pattern_counter++;
        }
    }
    else {
        while (next_entry(usecase, idx, stamp[idx], &cursor[idx])) {
            cursor[idx+1] = 0;
            generate_usecases(max_active, source, usecase,
                              cursor, stamp, seqno, idx+1);
        }
    }
}


static size_t print_entry(entry_t entry, char *buf, size_t len)
{
    conn_t conn;
    sink_t sink;
    stamp_t stamp;
    state_t state;
    const char *sink_name, *state_name;

    conn  = ENTRY_CONNECTION(entry);
    state = ENTRY_STATE(entry);
    stamp = ENTRY_STAMP(entry);
    sink  = CONNECTION_SINK(conn);

    sink_name = (sink >= 0 && sink < SINK_MAX) ?
                  sink_names[sink] : "<unknown>";
    state_name = (state >= 0 && state < STATE_MAX) ?
                  state_names[state] : "<unknown>";

    return snprintf(buf, len, "%s,%u,%s, ", sink_name, stamp, state_name);
}


void initialize_pattern_generator(const char *stem,
                                  int max_active,
                                  source_t source)
{
    const char *srcnam;
    char data_file[1024];
    char names_file[1024];

    if (source == allSources) {
        snprintf(data_file, sizeof(data_file), "%s-%d.data",
                 stem, max_active);
        snprintf(names_file, sizeof(names_file), "%s-%d.names",
                 stem, max_active);
    }
    else {
        srcnam = get_source_name(source);
        snprintf(data_file, sizeof(data_file), "%s-%s-%d.data",
                 stem, srcnam, max_active);
        snprintf(names_file, sizeof(names_file), "%s-%s-%d.names",
                 stem, srcnam, max_active);
    }

    if (!(data = fopen(data_file, "w+"))) {
        printf("failed to open file '%s': %s\n", data_file, strerror(errno));
        exit(errno);
    }

    if (!(names = fopen(names_file, "w+"))) {
        printf("failed to open file '%s': %s\n", names_file, strerror(errno));
        exit(errno);
    }

    printf("populating '%s' and '%s' files\n", data_file, names_file);

    populate_active_patterns();
    populate_sequences();
    populate_indices();
}

void generate_patterns(int max_active, int source)
{
    uint32_t seqno;
    entry_t cursor[SOURCE_MAX];
    stamp_t stamp[SOURCE_MAX];
    usecase_t usecase;
    source_t idx;

    for (seqno = 0;  seqno < seqno_max(max_active); seqno++) {
        memset(cursor, 0, sizeof(cursor));
        memset(usecase, 0, sizeof(usecase));

        for (idx = 0;  idx < SOURCE_MAX;  idx++) {
            stamp[idx] = source_stamp(seqno, idx, max_active);
        }

        generate_usecases(max_active, source, usecase, cursor, stamp, seqno, 0);
    }
}

void generate_names(int max_active, int source)
{
    const char *srcnam;
    sink_t sink;
    char feature[256];
    int  i,j;
    char *sep;


    fprintf(names, "decision.\n\n");

    for (i = 0;  i < SOURCE_MAX;  i++) {
        if ((srcnam = source_names[i])) {
            /* route */
            snprintf(feature, sizeof(feature), "%sRoute:", srcnam);
            fprintf(names, "%-24s noroute", feature);
            for (j = 1;  j < SINK_MAX;  j++) {
                if ((sink = conn_defs[i][j].sink) != noroute)
                    fprintf(names, ", %s", sink_names[j]);
            }
            fprintf(names, ".\n");

            /* stamp */
            snprintf(feature, sizeof(feature), "%sStamp:", srcnam);
            if (1) {
                fprintf(names, "%-24s 0", feature);
                for (j = 0; j < max_active; j++)
                    fprintf(names, ", %d", j+1);
                fprintf(names, ".\n");
            }
            else {
                fprintf(names, "%-24s continuous.\n", feature);
            }

            /* state */
            snprintf(feature, sizeof(feature), "%sState:", srcnam);
            fprintf(names, "%-24s stop, pause, play.\n\n", feature);
        }
    }

    snprintf(feature, sizeof(feature), "decision:");
    fprintf(names, "%-24s ", feature);

    if (source == allSources) {
        for (i = j = 0, sep = "";   i < DECISION_SPACE_DIMENSION;   i++) {
            if (decisions[i]) {
                decision_counter++;
                if (j && !(j % 10))
                    sep = ",\n                         ";
                fprintf(names, "%s%d", sep, i);
                sep = ", ";
                j++;
            }
        }
    }
    else {
        for (i = 0, sep = "";   i < CONN_STATE_MAX;  i++) {
            if (decisions[i]) {
                decision_counter++;
                fprintf(names, "%s%s", sep, get_conn_state_name(i));
                sep = ", ";
            }
        }
    }

    fprintf(names, ".\n");
}


size_t print_usecase(entry_t *usecase, char *buf, size_t len)
{
    source_t source;
    char *p, *e;

    e = (p = buf) + len;

    for (source = 0;  source < SOURCE_MAX && p < e;  source++)
        p += print_entry(usecase[source], p, e-p);

    return p - buf;
}


bool route_conflicts(entry_t entry, conn_t conn)
{
    conn_t c;
    source_t source;
    sink_t sink;
    conn_t *conflict;
    int i;

    if (CONNECTION_SINK(conn) == noroute)
        return false;

    c      = ENTRY_CONNECTION(entry);
    source = CONNECTION_SOURCE(c);
    sink   = CONNECTION_SINK(c);

    conflict = conn_defs[source][sink].conflict;

    for (i = 0;  conflict[i];  i++) {
        if (conn == conflict[i])
            return true;
    }

    return false;
}

const char *get_source_name(source_t source)
{
    if (source == allSources)
        return "allSources";
    if (source < 0 || source >= SOURCE_MAX)
        return "<unknown source>";
    return source_names[source];
}

const char *get_sink_name(sink_t sink)
{
    if (sink == allSinks)
        return "allSinks";
    if (sink < 0 || sink >= SINK_MAX)
        return "<unknown sink>";
    return sink_names[sink];
}

static void usage(int argc, char **argv, int status)
{
    (void)argc;

    printf("Usage: %s stem max_active_sources [source]\n"
           "\twhere\n"
           "\t   stem is the name for .data and .names files\n"
           "\t   range for max_active_sources (1 - %d)\n"
           "\t   the source the decisions are done for [optional]\n",
           basename(argv[0]), SOURCE_MAX);

    if (status)
        exit(status);
}

double get_time_stamp(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) < 0) {
        printf("failed to get time stamp: %s\n", strerror(errno));
        exit(errno);
    }

    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

int main(int argc, char **argv)
{
    char *stem;
    int max_active;
    int source;
    char *e;
    double start, end;

    if (argc > 4 || argc < 3)
        usage(argc, argv, EINVAL);

    stem = argv[1];
    max_active = strtol(argv[2], &e, 10);

    if (max_active < 1 || max_active >= SOURCE_MAX || *e || e == argv[2])
        usage(argc, argv, EINVAL);

    if (argc == 3)
        source = allSources;
    else {
        if (isdigit(argv[3][0])) {
            source = strtol(argv[3], &e, 10);

            if (*e || e == argv[3])
                source = SOURCE_MAX;
        }
        else if (isalpha(argv[3][0])) {
            for (source = 0;  source < SOURCE_MAX;  source++) {
                if (!strcmp(get_source_name(source), argv[3]))
                    break;
            }
        }
        else {
            source = SOURCE_MAX;
        }
    }

    if (source < -1 || source >= SOURCE_MAX)
        usage(argc, argv, EINVAL);

    start = get_time_stamp();

    initialize_pattern_generator(stem, max_active, source);
    generate_patterns(max_active, source);
    generate_names(max_active, source);

    end = get_time_stamp();

    printf("generated %u patterns with %u different decisions "
           "in %.3lf seconds\n",
           pattern_counter, decision_counter, end - start);

    return 0;
}
