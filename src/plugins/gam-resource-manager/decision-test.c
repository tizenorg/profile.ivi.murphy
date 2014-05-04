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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <ctype.h>

#include <murphy/common.h>
#include <breedline/breedline-murphy.h>

#include "decision-tree.h"
#include "c5-decision-tree.h"

#define INVALID_OFFSET      (~(size_t)0)

typedef struct {
    int32_t route;
    int32_t stamp;
    int32_t state;
} source_attr_t;

typedef struct {
    source_attr_t   wrtApplication;
    source_attr_t   icoApplication;
    source_attr_t   phone;
    source_attr_t   radio;
    source_attr_t   microphone;
    source_attr_t   navigator;
} usecase_vector_t;

typedef struct {
    const char *name;
    size_t offset;
} offset_def_t;

typedef struct {
    const char *name;
    size_t stamp_offset;
    size_t route_offset;
    size_t state_offset;
    mrp_decision_conf_t *conf;
    mrp_decision_tree_t *tree;
    struct {
        int32_t value;
        const char *str;
        bool valid;
        int32_t stamp;
        int32_t state;
    } decision;
} source_t;

typedef struct {
    const char *prognam;
    int max_active;
    mrp_mainloop_t *ml;
    brl_t *brl;
    int nsource;
    source_t *sources;
    mrp_decision_conf_t *default_conf;
    usecase_vector_t usecase;
} user_data_t;


#define OFFSET_DEF(_src,_attr,_fld)  \
    { # _src #_attr, MRP_OFFSET(usecase_vector_t, _src._fld) }
#define LIST_END                     \
    { NULL, 0 }
#define SOURCE(_src)                 \
    OFFSET_DEF(_src, Route, route),  \
    OFFSET_DEF(_src, Stamp, stamp),  \
    OFFSET_DEF(_src, State, state)

static offset_def_t offset_defs[] = {
    SOURCE( wrtApplication ),
    SOURCE( icoApplication ),
    SOURCE( phone          ),
    SOURCE( radio          ),
    SOURCE( microphone     ),
    SOURCE( navigator      ),
    LIST_END
};

#undef SOURCE
#undef LIST_END
#undef OFFSET_DEF


static bool whitespace(char **buf)
{
    char *p, *q, c;

    for (p = q = *buf;   (c = *p);   p++) {
        if (c != ' ' && c != '\t' && c != '\n')
            break;
    }

    *buf = p;

    return *p == 0 || p > q;
}

static bool identifier(char **buf, char *id, size_t len)
{
    char *p = *buf;
    char *q, *e, c;

    whitespace(&p);

    if (!isalpha(*(q = p)))
        return false;

    while ((c = *p)) {
        if (c == ' ' || c == '\t' || c == '\n')
            break;
        if (!isalnum(c) && c != '_' && c != '-')
            return false;
        p++;
    }

    if (p > q && (p-q) < ((int)len-1)) {
        e = p;

        if (whitespace(&p)) {
            strncpy(id, q, (e-q));
            id[(e-q)] = 0;

            *buf = p;

            return true;
        }
    }

    return false;
}

static bool integer(char **buf, int32_t *integer, char *string, size_t len)
{
    char *p = *buf;
    char *e;
    size_t l;

    whitespace(&p);


    *integer = strtol(p, &e, 10);

    if (e > p) {
        l = e - p;

        if (whitespace(&e)) {
            if (string && len > 0 && len >= l) {
                strncpy(string, p, l);
                string[l] = 0;
            }

            *buf = e;

            return true;
        }
    }

    return false;
}


static bool token(char **buf, const char *tkn)
{
    char *p = *buf;
    size_t l = strlen(tkn);

    whitespace(&p);

    if (!strncmp(p, tkn, l)) {
        p += l;
        if (whitespace(&p)) {
            *buf = p;
            return true;
        }
    }

    return false;
}

static bool end_of_line(char **buf)
{
    char *p = *buf;

    if (whitespace(&p) && *p == 0) {
        *buf = p;
        return true;
    }

    return false;
}

static source_t *find_source(user_data_t *ud, const char *srcnam)
{
    source_t *src;
    int i;

    for (i = 0;  i < ud->nsource;  i++) {
        src = ud->sources + i;

        if (!strcmp(srcnam, src->name))
            return(src);
    }

    return NULL;
}

static size_t find_offset(user_data_t *ud, const char *attrnam)
{
    offset_def_t *d;

    MRP_UNUSED(ud);

    for (d = offset_defs;  (d->name);  d++) {
        if (!strcmp(attrnam, d->name))
            return d->offset;
    }

    return INVALID_OFFSET;
}


static void show_conf(user_data_t *ud, const char *srcnam)
{
    source_t *src;
    char buf[32768];

    brl_hide_prompt(ud->brl);

    if (!(src = find_source(ud, srcnam)))
        printf("don't know anything about '%s' source\n", srcnam);
    else {
        mrp_decision_conf_print(src->conf, buf, sizeof(buf));
        printf("\n%s\n", buf);
    }

    brl_show_prompt(ud->brl);
}

static void show_tree(user_data_t *ud, const char *srcnam)
{
    source_t *src;
    char buf[32768];

    brl_hide_prompt(ud->brl);

    if (!(src = find_source(ud, srcnam)))
        printf("don't know anything about '%s' source\n", srcnam);
    else {
        mrp_decision_tree_print(src->conf, src->tree, buf, sizeof(buf));
        printf("\n%s\n", buf);
    }

    brl_show_prompt(ud->brl);
}

static void show_usecase(user_data_t *ud)
{
    mrp_decision_conf_t *conf;
    usecase_vector_t *usecase;
    offset_def_t *d;
    int32_t value;
    const char *vnam;
    char buf[256];

    brl_hide_prompt(ud->brl);

    if (!(conf = ud->default_conf))
        printf("\ncan't find any valid source configuration\n\n");
    else {
        printf("\n");

        usecase = &ud->usecase;

        for (d = offset_defs;  (d->name);   d++) {
            if ((d - offset_defs) && !((d - offset_defs) % 3))
                printf("\n");

            value = *(int32_t *)((char *)usecase + d->offset);
            vnam = mrp_decision_get_integer_attr_name(conf, d->name,value, NULL);

            snprintf(buf, sizeof(buf), "%s:", d->name);
            printf("%-24s %d (%s)\n", buf, value, vnam);
        }

        printf("\n");
    }

    brl_show_prompt(ud->brl);
}


static void show_sources(user_data_t *ud)
{
    int i;

    brl_hide_prompt(ud->brl);

    printf("\n");

    if (ud->nsource <= 0)
        printf("<no source available>\n");
    else {
        for (i = 0;  i < ud->nsource;  i++)
            printf("%s\n", ud->sources[i].name);
    }

    printf("\n");

    brl_show_prompt(ud->brl);
}


static void show_help(user_data_t *ud)
{
    brl_hide_prompt(ud->brl);
    printf("\nAvailable commands:\n"
           "    show usecase\n"
           "    show sources\n"
           "    show <source> {conf|tree}\n"
           "    set <source> {route|stamp|state} <value>\n"
           "    set * {route|state} <value>\n"
           "    decide\n"
           "    shift stamps <value>\n"
           "    reset stamps\n"
           "    normalise\n"
           "    apply\n"
           "    exit\n\n");

    brl_show_prompt(ud->brl);
}

static void show_cmd(user_data_t *ud, const char *cmd)
{
    char *p = (char *)cmd;
    char srcnam[256];

    if (whitespace(&p)) {
        if (token       (&p, "usecase") &&
            end_of_line (&p           )  )
        {
            show_usecase(ud);
            return;
        }
        else if (token       (&p, "sources") &&
                 end_of_line (&p           )  )
        {
            show_sources(ud);
            return;
        }
        else if (identifier  (&p, srcnam, sizeof(srcnam))) {
            if (token       (&p, "conf") &&
                end_of_line (&p        )  )
            {
                show_conf(ud, srcnam);
                return;
            }
            else if (token       (&p, "tree") &&
                     end_of_line (&p        )  )
            {
                show_tree(ud, srcnam);
                return;
            }
        }
    }

    brl_hide_prompt(ud->brl);
    printf("\nSyntax error. syntax of 'show' command:\n"
           "    show usecase\n"
           "    show sources\n"
           "    show <source> {conf|tree}\n\n");
    brl_show_prompt(ud->brl);
}


static void set_usecase_attribute(user_data_t *ud, const char *srcnam,
                                  const char *attrnam, const char *attrval)
{
    source_t *src;
    int32_t value;
    size_t offset;
    char fullnam[256];
    bool error;

    brl_hide_prompt(ud->brl);

    if (!(src = find_source(ud, srcnam)))
        printf("don't know anything about '%s' source\n", srcnam);
    else {
        snprintf(fullnam, sizeof(fullnam), "%s%s", srcnam, attrnam);
        value = mrp_decision_get_integer_attr_value(src->conf, fullnam,
                                                    attrval, &error);
        offset = find_offset(ud, fullnam);

        if (error || offset == INVALID_OFFSET)
            printf("invalid attribute '%s' or value '%s'\n", fullnam, attrval);
        else
            *(int32_t *)((char *)&ud->usecase + offset) = value;
    }

    brl_show_prompt(ud->brl);
}

static void set_cmd(user_data_t *ud, const char *cmd)
{
    char *p = (char *)cmd;
    char srcnam[256];
    char attrval[256];
    int32_t stamp;
    int i;


    if (whitespace (&p)) {
        if (token(&p, "*")) {
            if (token       (&p, "route"                 ) &&
                identifier  (&p, attrval, sizeof(attrval)) &&
                end_of_line (&p                          )  )
            {
                for (i = 0;  i < ud->nsource;  i++) {
                    set_usecase_attribute(ud, ud->sources[i].name,
                                          "Route", attrval);
                }
                return;
            }
            if (token       (&p, "state"                 ) &&
                identifier  (&p, attrval, sizeof(attrval)) &&
                end_of_line (&p                          )  )
            {
                for (i = 0;  i < ud->nsource;  i++) {
                    set_usecase_attribute(ud, ud->sources[i].name,
                                          "State", attrval);
                }
                return;
            }
        }
        if (identifier  (&p, srcnam,  sizeof(srcnam))) {
            if (token       (&p, "route"                 ) &&
                identifier  (&p, attrval, sizeof(attrval)) &&
                end_of_line (&p                          )  )
            {
                set_usecase_attribute(ud, srcnam, "Route", attrval);
                return;
            }
            if (token       (&p, "stamp"                         ) &&
                integer     (&p, &stamp, attrval, sizeof(attrval)) &&
                end_of_line (&p                                  )  )
            {
                set_usecase_attribute(ud, srcnam, "Stamp", attrval);
                return;
            }
            if (token       (&p, "state"                 ) &&
                identifier  (&p, attrval, sizeof(attrval)) &&
                end_of_line (&p                          )  )
            {
                set_usecase_attribute(ud, srcnam, "State", attrval);
                return;
            }
        }
    }

    brl_hide_prompt(ud->brl);
    printf("\nSyntax error. syntax of 'set' command:\n"
           "    set <source> {route|stamp|state} <value>\n"
           "    set * {route|state} <value>\n\n");
    brl_show_prompt(ud->brl);
}


static void decide(user_data_t *ud)
{
    mrp_decision_value_type_t type;
    mrp_decision_value_t *decision;
    source_t *src;
    mrp_decision_conf_t *conf;
    int32_t stop, pause, play;
    size_t offs;
    char buf[256];
    int i;


    for (i = 0;  i < ud->nsource;  i++) {
        src  = ud->sources + i;
        conf = src->conf;

        snprintf(buf, sizeof(buf), "%sState", src->name);

        stop  = mrp_decision_get_integer_attr_value(conf, buf, "stop",  NULL);
        pause = mrp_decision_get_integer_attr_value(conf, buf, "pause", NULL);
        play  = mrp_decision_get_integer_attr_value(conf, buf, "play",  NULL);

        if (!mrp_decision_make(src->tree, &ud->usecase, &type, &decision)) {
            src->decision.value = 0;
            src->decision.str = "<failed>";
            src->decision.valid = false;
        }
        else {
            src->decision.value = decision->integer;
            src->decision.str = mrp_decision_name(src->conf, decision->integer);
            src->decision.valid = true;

            offs = src->stamp_offset;
            src->decision.stamp = *(int32_t *)((char *)&ud->usecase + offs);

            if (!strcmp(src->decision.str, "disconnected"))
                src->decision.state = stop;
            else if (!strcmp(src->decision.str, "connected"))
                src->decision.state = play;
            else if (!strcmp(src->decision.str, "suspended"))
                src->decision.state = pause;
            else {
                src->decision.stamp = 0;
                src->decision.state = stop;
            }
        }
    } /* for  */
}

static void decide_cmd(user_data_t *ud, const char *cmd)
{
    source_t *src;
    bool stamp_error;
    size_t ioffs, joffs;
    int32_t istamp, jstamp, stamp_max;
    uint32_t stamp_mask, full_mask;
    int32_t new_stamp, new_state;
    int32_t old_stamp, old_state;
    char nambuf[256];
    char valbuf[256];
    char statebuf[256];
    char stampbuf[256];
    const char *old_name, *new_name;
    int i,j;
    bool ok;

    MRP_UNUSED(cmd);

    brl_hide_prompt(ud->brl);


    if (ud->nsource < 1)
        printf("\nthere are no valid sources\n\n");
    else {
        printf("\n");

        stamp_error = false;
        stamp_mask = 0;
        stamp_max  = 0;

        for (i = 0;    i < ud->nsource;    i++) {
            ioffs = ud->sources[i].stamp_offset;
            istamp = *(int32_t *)((char *)&ud->usecase + ioffs);

            if (istamp > 0) {
                if (istamp > stamp_max)
                    stamp_max = istamp;

                stamp_mask |= (uint32_t)1 << (istamp - 1);
            }

            for (j = i + 1;  j < ud->nsource;  j++) {
                joffs = ud->sources[j].stamp_offset;
                jstamp = *(int32_t *)((char *)&ud->usecase + joffs);

                if (istamp > 0 && istamp == jstamp) {
                    printf("stamp %d occurs multiple times\n", istamp);
                    stamp_error = true;
                }
            }
        }

        if (stamp_max > 0) {
            full_mask = ((uint32_t)1 << stamp_max) - 1;

            if (stamp_mask != full_mask) {
                printf("stamps are not continuous\n");
                stamp_error = true;
            }
        }


        if (!stamp_error) {
            decide(ud);

            for (i = 0;  i < ud->nsource;  i++) {
                src = ud->sources + i;

                old_stamp = *(int32_t*)((char*)&ud->usecase+src->stamp_offset);
                old_state = *(int32_t*)((char*)&ud->usecase+src->state_offset);

                new_state = src->decision.state;
                new_stamp = src->decision.stamp;

                statebuf[0] = 0;
                stampbuf[0] = 0;

                if (src->decision.valid && old_state != new_state) {
                    snprintf(nambuf, sizeof(nambuf), "%sState", src->name);
                    old_name = mrp_decision_get_integer_attr_name(src->conf,
                                                                  nambuf,
                                                                  old_state,
                                                                  &ok);
                    new_name = mrp_decision_get_integer_attr_name(src->conf,
                                                                  nambuf,
                                                                  new_state,
                                                                  &ok);
                    snprintf(statebuf, sizeof(statebuf), "%d (%s) => %d (%s)",
                             old_state, old_name, new_state, new_name);
                }

                if (src->decision.valid && old_stamp != new_stamp) {
                    snprintf(stampbuf, sizeof(stampbuf),"%d => %d",
                             old_stamp, new_stamp);
                }

                snprintf(nambuf, sizeof(nambuf), "%s:",
                         src->name);
                snprintf(valbuf, sizeof(valbuf), "%d (%s)",
                         src->decision.value, src->decision.str);

                printf("%-24s %-18s %-24s %s\n",
                       nambuf, valbuf, statebuf, stampbuf);
            }
        }

        printf("\n");
    }

    brl_show_prompt(ud->brl);
}


static void shift_cmd(user_data_t *ud, const char *cmd)
{
    char *p = (char *)cmd;
    int32_t shift;
    size_t stamp_offset;
    int32_t *stamp_ptr;
    int32_t stamp;
    int i;

    if (ud->max_active < 2) {
        brl_hide_prompt(ud->brl);
        printf("\nshift is not allowed if maximum 1 stream can be active\n\n");
        brl_show_prompt(ud->brl);
    }
    else {
        if (whitespace  (&p                ) &&
            token       (&p, "stamps"      ) &&
            integer     (&p, &shift, NULL,0) &&
            end_of_line (&p                )  )
        {
            if (shift  > -ud->max_active &&
                shift !=  0              &&
                shift  <  ud->max_active  )
            {
                for (i = 0;  i < ud->nsource;  i++) {
                    stamp_offset = ud->sources[i].stamp_offset;
                    stamp_ptr = (int32_t*)((char*)&ud->usecase + stamp_offset);

                    if ((stamp = *stamp_ptr) > 0) {
                        stamp += shift;

                        if (stamp < 0 || stamp > ud->max_active)
                            stamp = 0;

                        *stamp_ptr = stamp;
                    }
                } /* for */

                show_usecase(ud);

                return;
            }
        }
    }

    brl_hide_prompt(ud->brl);
    printf("\nSyntax error. syntax of 'shift' command:\n"
           "    shift stamps <value>\n"
           "where\n"
           "    <value> should be in the range of [%-d:-1] or [1:%d]\n\n",
           -(ud->max_active - 1), (ud->max_active - 1));
    brl_show_prompt(ud->brl);
}

static void reset_cmd(user_data_t *ud, const char *cmd)
{
    char *p = (char *)cmd;
    size_t stamp_offset;
    int32_t *stamp_ptr;
    int i;

    if (whitespace  (&p                ) &&
        token       (&p, "stamps"      ) &&
        end_of_line (&p                )  )
    {
        for (i = 0;  i < ud->nsource;  i++) {
            stamp_offset = ud->sources[i].stamp_offset;
            stamp_ptr = (int32_t*)((char*)&ud->usecase + stamp_offset);

            *stamp_ptr = 0;
        }

        show_usecase(ud);

        return;
    }

    brl_hide_prompt(ud->brl);
    printf("\nSyntax error. syntax of 'reset' command:\n"
           "    reset stamps\n\n");
    brl_show_prompt(ud->brl);
}

static void normalise(user_data_t *ud)
{
    source_t *src;
    size_t route_offset, stamp_offset, state_offset;
    int32_t *route_ptr, *stamp_ptr, *state_ptr;
    int nptr;
    int32_t *ptrs[ud->nsource];
    int32_t *tmp;
    int i, j;

    for (nptr = 0, i = 0;  i < ud->nsource;  i++) {
        src = ud->sources + i;

        route_offset = src->route_offset;
        stamp_offset = src->stamp_offset;
        state_offset = src->state_offset;

        route_ptr = (int32_t *)((char *)&ud->usecase + route_offset);
        stamp_ptr = (int32_t *)((char *)&ud->usecase + stamp_offset);
        state_ptr = (int32_t *)((char *)&ud->usecase + state_offset);

        if (*stamp_ptr > 0)
            ptrs[nptr++] = stamp_ptr;
        else {
            *route_ptr = 0;
            *stamp_ptr = 0;
            *state_ptr = 0;
        }
    }

    for (i = 0;   i < nptr-1;   i++) {
        for (j = i + 1;  j < nptr;  j++) {
            if (*(ptrs[i]) > *(ptrs[j])) {
                tmp = ptrs[i];
                ptrs[i] = ptrs[j];
                ptrs[j] = tmp;
            }
        }
    }

    for (i = 0;  i < nptr;  i++)
        *(ptrs[i]) = i+1;
}

static void normalise_cmd(user_data_t *ud, const char *cmd)
{
    char *p = (char *)cmd;

    if (end_of_line (&p)) {
        normalise(ud);

        show_usecase(ud);

        return;
    }

    brl_hide_prompt(ud->brl);
    printf("\nSyntax error. syntax of 'normalise' command:\n"
           "    normalise\n\n");
    brl_show_prompt(ud->brl);
}

static void apply_cmd(user_data_t *ud, const char *cmd)
{
    char *p = (char *)cmd;
    source_t *src;
    int32_t *stamp_ptr, *state_ptr;
    int i, n;

    if (end_of_line (&p)) {
        for (n = i = 0;  i < ud->nsource;  i++) {
            src = ud->sources + i;

            if (src->decision.valid) {
                src->decision.valid = false;

                stamp_ptr = (int32_t*)((char*)&ud->usecase + src->stamp_offset);
                state_ptr = (int32_t*)((char*)&ud->usecase + src->state_offset);

                if (*stamp_ptr != src->decision.stamp) {
                    *stamp_ptr = src->decision.stamp;
                    n++;
                }

                if (*state_ptr != src->decision.state) {
                    *state_ptr = src->decision.state;
                    n++;
                }
            }
        }

        if (n == 0) {
            brl_hide_prompt(ud->brl);
            printf("\nnothing has been changed\n\n");
            brl_show_prompt(ud->brl);
        }
        else {
            normalise(ud);
            show_usecase(ud);
        }

        return;
    }

    brl_hide_prompt(ud->brl);
    printf("\nSyntax error. syntax of 'apply' command:\n"
           "    apply\n\n");
    brl_show_prompt(ud->brl);
}

static void input_handler(brl_t *brl, const char *input, void *user_data)
{
    user_data_t *ud = (user_data_t *)user_data;

    MRP_UNUSED(brl);

    brl_add_history(ud->brl, input);

    if (input == NULL || !strcmp(input, "exit")) {
        mrp_mainloop_quit(ud->ml, 0);
        return;
    }

    if (strlen(input) == 0)
        return;

    if (!strcmp(input, "help")) {
        show_help(ud);
    }
    else if (!strncmp(input, "set", 3)) {
        set_cmd(ud, input+3);
    }
    else if (!strncmp(input, "show", 4)) {
        show_cmd(ud, input+4);
    }
    else if (!strncmp(input, "decide", 6)) {
        decide_cmd(ud, input+4);
    }
    else if (!strncmp(input, "shift", 5)) {
        shift_cmd(ud, input+5);
    }
    else if (!strncmp(input, "reset", 5)) {
        reset_cmd(ud, input+5);
    }
    else if (!strncmp(input, "normalise", 9)) {
        normalise_cmd(ud, input+9);
    }
    else if (!strncmp(input, "apply", 5)) {
        apply_cmd(ud, input+5);
    }
    else {
        show_help(ud);
    }
}


static void signal_handler(mrp_sighandler_t *h, int signum, void *user_data)
{
    mrp_mainloop_t *ml = mrp_get_sighandler_mainloop(h);

    MRP_UNUSED(user_data);

    switch (signum) {
    case SIGINT:
        printf("Got SIGINT\n");
        if (ml != NULL)
            mrp_mainloop_quit(ml, 0);
        else
            exit(0);
        break;
    }
}

static void input_create(user_data_t *ud, const char *prompt)
{
    if (!(ud->ml = mrp_mainloop_create())) {
        printf("failed to create mainloop\n");
        exit(1);
    }

    mrp_add_sighandler(ud->ml, SIGINT, signal_handler, NULL);

    ud->brl = brl_create_with_murphy(fileno(stdin), prompt, ud->ml,
                                     input_handler, ud);
    if (!ud->brl) {
        printf("failed to create breedline\n");
        exit(1);
    }

    brl_show_prompt(ud->brl);
}

static void input_destroy(user_data_t *ud)
{
    if (ud) {
        if (ud->brl)
            brl_destroy(ud->brl);
        if (ud->ml)
            mrp_mainloop_destroy(ud->ml);
    }
}

static void input_process(user_data_t *ud)
{
    if (ud && ud->ml)
        mrp_mainloop_run(ud->ml);
}

static void sources_create(user_data_t *ud, const char *prefix)
{
    mrp_decision_conf_t *conf;
    mrp_decision_tree_t *tree;
    offset_def_t *d1, *d2;
    source_t *src;
    char srcnam[256];
    size_t srcnamlen;
    char stem[1024];
    size_t offset;
    char *p;
    size_t dlen;
    int idx;
    size_t size;
    int i;
    int errcnt;

    memset(srcnam, 0, sizeof(srcnam));

    conf = NULL;
    tree = NULL;

    for (srcnamlen = 0, d1 = offset_defs;   (d1->name);    d1++) {

        offset = d1->offset;

        if ((p = strstr(d1->name, "Route"))) {
            dlen = p - d1->name;

            if (dlen >= sizeof(srcnam) - 1) {
                printf("skipping source with too long name\n");
                continue;
            }

            if (srcnamlen == dlen && !strncmp(srcnam, d1->name, dlen))
                continue;

            /* seems to be new source name */
            srcnamlen = dlen;

            strncpy(srcnam, d1->name, dlen);
            srcnam[dlen] = 0;

            if (prefix) {
                snprintf(stem, sizeof(stem), "%s-%s-%d",
                         prefix, srcnam, ud->max_active);
            }
            else {
                snprintf(stem, sizeof(stem), "%s-%d",
                         srcnam, ud->max_active);
            }


            if (!(conf = mrp_decision_conf_create_from_file(stem)))
                goto failed;

            for (errcnt = 0, d2 = offset_defs;  d2->name;  d2++) {
                if (!mrp_decision_set_attr_offset(conf, d2->name, d2->offset)) {
                    errcnt++;
                    printf("failed to set offset of '%s' for source %s\n",
                           d2->name, srcnam);
                }
            }

            if (errcnt > 0)
                goto failed;

            if (!(tree = mrp_decision_tree_create_from_file(conf, NULL)))
                goto failed;

            idx = ud->nsource++;
            size = sizeof(source_t) * ud->nsource;

            if (!(ud->sources = mrp_realloc(ud->sources, size))) {
                printf("failed to (re)allocate memory for %s source", srcnam);
                ud->nsource = 0;
                goto failed;
            }

            src = ud->sources + idx;

            memset(src, 0, sizeof(source_t));
            src->name = mrp_strdup(srcnam);
            src->route_offset = offset;
            src->conf = conf;
            src->tree = tree;

            if (!ud->default_conf)
                ud->default_conf = conf;

            conf = NULL;
            tree = NULL;

            continue;

        failed:
            if (tree) {
                mrp_decision_tree_destroy(tree);
                tree = NULL;
            }
            if (conf) {
                mrp_decision_conf_destroy(conf);
                conf = NULL;
            }
        } /* if Route */
        else if ((p = strstr(d1->name, "Stamp"))) {
            dlen = p - d1->name;

            for (i = 0;  i < ud->nsource;  i++) {
                src = ud->sources + i;
                if (!strncmp(src->name, d1->name, dlen)) {
                    src->stamp_offset = offset;
                    break;
                }
            }
        } /* if Stamp */
        else if ((p = strstr(d1->name, "State"))) {
            dlen = p - d1->name;

            for (i = 0;  i < ud->nsource;  i++) {
                src = ud->sources + i;
                if (!strncmp(src->name, d1->name, dlen)) {
                    src->state_offset = offset;
                    break;
                }
            }
        }
    } /* for */
}


int main(int argc, char **argv)
{
    user_data_t ud;
    const char *prefix;
    int max_active;
    const char *prompt;

    MRP_UNUSED(argc);

    prompt = "decision-tester";
    prefix = "gam";
    max_active = 4;

    memset(&ud, 0, sizeof(ud));
    ud.prognam = basename(argv[0]);
    ud.max_active = max_active;

    sources_create(&ud, prefix);
    input_create(&ud, prompt);

    input_process(&ud);

    printf("Exiting now ...\n");

    input_destroy(&ud);

    return 0;
}
