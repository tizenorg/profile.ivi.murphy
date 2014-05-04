#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decision-maker.h"

#define CONN_STATE_MASK                         \
    (((decision_t)1 << CONN_STATE_BITS) - 1)

#define KEY(_priority,_stamp)             \
    ((_stamp) ? ((((_priority) & 0xff) << 8) |   \
                 ((_stamp) & 0xff)            )  \
              :  0)

typedef uint32_t list_key_t;

typedef struct {
    list_key_t key;
    entry_t entry;
    state_t new_state;
} list_t;


static const char *conn_state_names[CONN_STATE_MAX] = {
    [teardown]     = "teardown",
    [disconnected] = "disconnected",
    [connected]    = "connected",
    [suspended]    = "suspended",
};


static uint32_t source_priority [SOURCE_MAX] = {
    [wrtApplication] = 1,
    [icoApplication] = 1,
    [phoneSource]    = 3,
    [radio]          = 1,
    [microphone]     = 3,
    [navigator]      = 2,
};

static bool mutually_exclusive[SOURCE_MAX][SOURCE_MAX] = {
    [wrtApplication][icoApplication] = true,
    [wrtApplication][radio]          = true,

    [icoApplication][wrtApplication] = true,
    [icoApplication][radio]          = true,

    [radio][wrtApplication]          = true,
    [radio][icoApplication]          = true,
};

static decision_t set_decision_for_source(source_t source, conn_state_t state)
{
    decision_t decision;

    if (source < 0 || source >= SOURCE_MAX)
        decision =  0;
    else
        decision = (((decision_t)state) & CONN_STATE_MASK) <<
                                                (source * CONN_STATE_BITS);
    return decision;
}

#if 0
static conn_state_t get_decision_for_source(source_t source, decision_t decision)
{
    conn_state_t state;

    if (source < 0 || source >= SOURCE_MAX)
        state = 0;
    else
        state = (decision >> (source * CONN_STATE_BITS)) & CONN_STATE_MASK;

    return state;
}
#endif

static void sort_list(list_t *list)
{
    list_t tmp;
    int i,j;

    for (i = 0;  i < SOURCE_MAX-1; i++) {
        for (j = i+1; j < SOURCE_MAX; j++) {
            if (list[j].key > list[i].key) {
                tmp = list[j];
                list[j] = list[i];
                list[i] = tmp;
            }
        }
    }
}


static void build_sorted_list(entry_t *usecase, list_t *list)
{
    list_t *l;
    stamp_t stamp;
    uint32_t priority;
    int i;

    for (i = 0;  i < SOURCE_MAX;  i++) {
        stamp = ENTRY_STAMP(usecase[i]);
        priority = source_priority[i];

        l = list + i;

        l->key = KEY(priority, stamp);
        l->entry = usecase[i];
    }

    sort_list(list);
}

static bool routing_conflict(list_t *list, int i)
{
    entry_t entry = list[i].entry;
    entry_t e;
    int j;

    if (ENTRY_STAMP(entry) > 0) {
        for (j = 0; j < i;  j++) {
            e = ENTRY_CONNECTION(list[j].entry);

            if (route_conflicts(entry, e))
                return true;
        }
    }

    return false;
}

static bool rule_conflict(list_t *list, int i)
{
    entry_t  entry  = list[i].entry;
    stamp_t  stamp  = ENTRY_STAMP(entry);
    conn_t   conn   = ENTRY_CONNECTION(entry);
    source_t source = CONNECTION_SOURCE(conn);
    entry_t  e;
    conn_t   c;
    source_t s;
    int j;

    if (stamp > 0) {
        for (j = 0;  j < i;  j++) {
            e = list[j].entry;
            c = ENTRY_CONNECTION(e);
            s = CONNECTION_SOURCE(c);

            if (ENTRY_STAMP(e) > 0 && list[j].new_state == play) {
                if (mutually_exclusive[source][s])
                    return true;
            }
        }
    }

    return false;
}



static conn_state_t determine_connection_state(list_t *list, int i)
{
    entry_t entry;
    conn_t conn;
    sink_t sink;
    conn_state_t ct;

    entry = list[i].entry;
    ct = teardown;

    if (ENTRY_STAMP(entry) > 0) {
        conn  = ENTRY_CONNECTION(entry);
        sink  = CONNECTION_SINK(conn);

        if (sink == noroute)
            ct = disconnected;
        else if (routing_conflict(list, i))
            ct = disconnected;
        else if (rule_conflict(list, i))
            ct = suspended;
        else
            ct = connected;

    }

    return ct;
}


decision_t make_decision(entry_t *usecase, source_t source, int max_active)
{
    decision_t decision;
    list_t list[SOURCE_MAX];
    entry_t entry;
    conn_t conn;
    source_t idx;
    stamp_t stamp;
    conn_state_t ct;
    int active;
    int i;

    build_sorted_list(usecase, list);
    decision = 0;
    active = 0;

    for (i = 0;  i < SOURCE_MAX;  i++) {
        entry = list[i].entry;
        stamp = ENTRY_STAMP(entry);
        conn  = ENTRY_CONNECTION(entry);
        idx   = CONNECTION_SOURCE(conn);

        if (active >= max_active || !stamp)
            ct = teardown;
        else {
            ct = determine_connection_state(list, i);
            active++;
        }

        if (source == allSources)
            decision |= set_decision_for_source(idx, ct);
        else if (source == idx) {
            decision = ct;
            break;
        }

        list[i].new_state = (ct == connected) ? play : stop;
    } /* for */

#if 0
    for (i=0; i<SOURCE_MAX; i++) {
        entry = list[i].entry;
        conn = ENTRY_CONNECTION(entry);
        printf("[0x%x %d %s -> %s %s] ",
               list[i].key,
               ENTRY_STAMP(entry),
               get_source_name(CONNECTION_SOURCE(conn)),
               get_sink_name(CONNECTION_SINK(conn)),
               ENTRY_STATE(entry) ? "play":"stop");
    }
    printf("=> %u\n", decision);
#endif

    return decision;
}

size_t print_decision(decision_t decision, source_t source, char *buf, size_t len)
{
    size_t l;

    if (source == allSources)
        l = snprintf(buf, len, "%u", decision);
    else
        l = snprintf(buf, len, "%s", get_conn_state_name(decision));

    return l;
}

const char *get_conn_state_name(conn_state_t ct)
{
    if (ct < 0 || ct >= CONN_STATE_MAX)
        return "<unknown>";
    return conn_state_names[ct];
}
