/*
 * Copyright (c) 2012, Intel Corporation
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

#include <stdarg.h>
#include <string.h>
#include <murphy/common.h>
#include <murphy/core.h>
#include "utils.h"
#include "ofono.h"


/**
 * oFono listener for Murphy
 *
 * For tracking voice calls handled by oFono, first all modems are listed and
 * tracked. Then, for each modem object which has VoiceCallManager interface,
 * VoiceCall objects are tracked and call information is updated to Murphy DB
 * using the provided listener callback (see telephony.c).
 */

#define _NOTIFY_MDB         1


#define OFONO_DBUS          "system"
#define OFONO_DBUS_PATH     "/org/ofono/"
#define OFONO_SERVICE       "org.ofono"
#define OFONO_MODEM_MGR     "org.ofono.Manager"
#define OFONO_MODEM         "org.ofono.Modem"
#define OFONO_CALL_MGR      "org.ofono.VoiceCallManager"
#define OFONO_CALL          "org.ofono.VoiceCall"


static int install_ofono_handlers(ofono_t *ofono);
static void remove_ofono_handlers(ofono_t *ofono);
static void ofono_init_cb(mrp_dbus_t *, const char *, int, const char *, void*);
static void purge_modems(ofono_t *ofono);
static int query_modems(ofono_t *ofono);
static int modem_removed_cb(mrp_dbus_t *, mrp_dbus_msg_t *, void *);
static int modem_added_cb(mrp_dbus_t *, mrp_dbus_msg_t *, void *);
static int modem_changed_cb(mrp_dbus_t *, mrp_dbus_msg_t *, void *);

static int query_calls(mrp_dbus_t *, ofono_modem_t *);
static void cancel_call_query(ofono_modem_t *modem);
static int call_changed_cb(mrp_dbus_t *, mrp_dbus_msg_t *, void *);
static int call_endreason_cb(mrp_dbus_t *, mrp_dbus_msg_t *, void *);
static int call_removed_cb(mrp_dbus_t *, mrp_dbus_msg_t *, void *);
static int call_added_cb(mrp_dbus_t *, mrp_dbus_msg_t *, void *);

static void dump_call(ofono_call_t *call);
static void purge_calls(ofono_modem_t *modem);


/******************************************************************************
 * The implementation of public methods of this file:
 * ofono_watch(), ofono_unwatch()
 */

void ofono_unwatch(ofono_t *ofono)
{
    if (MRP_LIKELY(ofono != NULL)) {
        remove_ofono_handlers(ofono);
        mrp_free(ofono);
    }
}


ofono_t *ofono_watch(mrp_mainloop_t *ml, tel_watcher_t notify)
{
    ofono_t *ofono;
    mrp_dbus_err_t dbuserr;
    mrp_dbus_t *dbus;

    mrp_debug("entering ofono_watch");

    dbus = mrp_dbus_connect(ml, OFONO_DBUS, &dbuserr);
    FAIL_IF_NULL(dbus, NULL, "failed to open %s DBUS", OFONO_DBUS);

    ofono = mrp_allocz(sizeof(*ofono));
    FAIL_IF_NULL(ofono, NULL, "failed to allocate ofono proxy");

    mrp_list_init(&ofono->modems);
    ofono->dbus = dbus;

    if (install_ofono_handlers(ofono)) {
        ofono->notify = notify;
        /* query_modems(ofono); */ /* will be done from ofono_init_cb */
        return ofono;
    } else {
        mrp_log_error("failed to set up ofono DBUS handlers");
        ofono_unwatch(ofono);
    }

    return NULL;
}


/*****************************************************************************
 * Wiring DBUS callbacks.
 */

static int install_ofono_handlers(ofono_t *ofono)
{
    const char *path;

    if (!mrp_dbus_follow_name(ofono->dbus, OFONO_SERVICE,
                              ofono_init_cb,(void *) ofono))
        goto fail;

    path  = "/";

    /** watch modem change signals */
    if (!mrp_dbus_subscribe_signal(ofono->dbus, modem_added_cb, ofono,
            OFONO_SERVICE, path, OFONO_MODEM_MGR, "ModemAdded", NULL)) {
        mrp_log_error("error watching ModemAdded on %s", OFONO_MODEM_MGR);
        goto fail;
    }

    /* TODO: check if really needed to be handled */
    if (!mrp_dbus_subscribe_signal(ofono->dbus, modem_removed_cb, ofono,
            OFONO_SERVICE, path, OFONO_MODEM_MGR, "ModemRemoved", NULL)) {
        mrp_log_error("error watching ModemRemoved on %s", OFONO_MODEM_MGR);
        goto fail;
    }

    /* TODO: check if really needed to be handled */
    if (!mrp_dbus_subscribe_signal(ofono->dbus, modem_changed_cb, ofono,
            OFONO_SERVICE, NULL, OFONO_MODEM, "PropertyChanged", NULL)) {
        mrp_log_error("error watching PropertyChanged on %s", OFONO_MODEM);
        goto fail;
    }

    /** watch call manager signals from a modem object */
    if (!mrp_dbus_subscribe_signal(ofono->dbus, call_added_cb, ofono,
            OFONO_SERVICE, NULL, OFONO_CALL_MGR, "CallAdded", NULL)) {
        mrp_log_error("error watching CallAdded on %s", OFONO_CALL_MGR);
        goto fail;
    }

    if (!mrp_dbus_subscribe_signal(ofono->dbus, call_removed_cb, ofono,
            OFONO_SERVICE, NULL, OFONO_CALL_MGR, "CallRemoved", NULL)) {
        mrp_log_error("error watching CallRemoved on %s", OFONO_CALL_MGR);
        goto fail;
    }

    /** watch call change signals from a call object */
    if (!mrp_dbus_subscribe_signal(ofono->dbus, call_changed_cb, ofono,
            OFONO_SERVICE, NULL, OFONO_CALL, "PropertyChanged", NULL)) {
        mrp_log_error("error watching PropertyChanged on %s", OFONO_CALL);
        goto fail;
    }

    if (!mrp_dbus_subscribe_signal(ofono->dbus, call_endreason_cb, ofono,
            OFONO_SERVICE, NULL, OFONO_CALL, "DisconnectReason", NULL)) {
        mrp_log_error("error watching DisconnectReason on %s", OFONO_CALL);
        goto fail;
    }

    mrp_debug("installed oFono signal handlers");
    return TRUE;

fail:
    remove_ofono_handlers(ofono);
    mrp_log_error("failed to install oFono signal handlers");
    return FALSE;
}


static void remove_ofono_handlers(ofono_t *ofono)
{
    const char *path;

    FAIL_IF_NULL(ofono,, "trying to remove handlers from NULL ofono");
    FAIL_IF_NULL(ofono->dbus,, "ofono->dbus is NULL");

    mrp_dbus_forget_name(ofono->dbus, OFONO_SERVICE,
                         ofono_init_cb, (void *) ofono);

    path  = "/";
    mrp_debug("removing DBUS signal watchers");

    mrp_dbus_unsubscribe_signal(ofono->dbus, modem_added_cb, ofono,
        OFONO_SERVICE, path, OFONO_MODEM_MGR, "ModemAdded", NULL);

    mrp_dbus_unsubscribe_signal(ofono->dbus, modem_removed_cb, ofono,
        OFONO_SERVICE, path, OFONO_MODEM_MGR, "ModemRemoved", NULL);

    mrp_dbus_unsubscribe_signal(ofono->dbus, modem_changed_cb, ofono,
        OFONO_SERVICE, NULL, OFONO_MODEM, "PropertyChanged", NULL);

    mrp_dbus_unsubscribe_signal(ofono->dbus, call_added_cb, ofono,
        OFONO_SERVICE, NULL, OFONO_CALL_MGR, "CallAdded", NULL);

    mrp_dbus_unsubscribe_signal(ofono->dbus, call_removed_cb, ofono,
        OFONO_SERVICE, NULL, OFONO_CALL_MGR, "CallRemoved", NULL);

    mrp_dbus_unsubscribe_signal(ofono->dbus, call_changed_cb, ofono,
        OFONO_SERVICE, NULL, OFONO_CALL, "PropertyChanged", NULL);

    mrp_dbus_unsubscribe_signal(ofono->dbus, call_changed_cb, ofono,
        OFONO_SERVICE, NULL, OFONO_CALL, "DisconnectReason", NULL);

}


static void ofono_init_cb(mrp_dbus_t *dbus,
                          const char *name,
                          int running,
                          const char *owner,
                          void *user_data)
{
    ofono_t *ofono = (ofono_t *)user_data;
    (void)dbus;
    (void)owner;

    FAIL_IF_NULL(ofono,, "ofono proxy is NULL");
    mrp_debug("%s is %s.", name, running ? "up" : "down");

    if (!running)
        purge_modems(ofono);
    else
        query_modems(ofono);
}



/******************************************************************************
 * modem objects' lifecycle
 */

static void dump_modem(ofono_modem_t *m)
{
    ofono_call_t     *call;
    mrp_list_hook_t  *p, *n;
    int               i;


    mrp_debug("modem '%s' {", m->modem_id);
    mrp_debug("    name:           %s", DUMP_STR(m->name));
    mrp_debug("    manufacturer:   %s", DUMP_STR(m->manufacturer));
    mrp_debug("    model:          %s", DUMP_STR(m->model));
    mrp_debug("    revision:       %s", DUMP_STR(m->revision));
    mrp_debug("    serial:         %s", DUMP_STR(m->serial));
    mrp_debug("    type:           %s", DUMP_STR(m->type));
    mrp_debug("    powered:        %s", m->powered ? "yes" : "no");
    mrp_debug("    online:         %s", m->online ? "yes" : "no");
    mrp_debug("    locked:         %s", m->lockdown ? "yes" : "no");
    mrp_debug("    emergency mode: %s", m->emergency ? "yes" : "no");

    mrp_debug("    interfaces:");
    if (m->interfaces != NULL) {
        for (i = 0; m->interfaces[i] != NULL; i++)
            mrp_debug("                    %s", m->interfaces[i]);
    }

    mrp_debug("    features:");
    if (m->features != NULL) {
        for (i = 0; m->features[i] != NULL; i++)
            mrp_debug("                    %s", m->features[i]);
    }

    mrp_debug("    calls:");
    if (!mrp_list_empty(&m->calls)) {
        mrp_list_foreach(&m->calls, p, n) {
            call = mrp_list_entry(p, ofono_call_t, hook);
            dump_call(call);
        }
    }

    mrp_debug("}");
}


static int free_modem(ofono_modem_t *modem)
{
    int i;

    if (modem != NULL) {
        mrp_list_delete(&modem->hook);

        cancel_call_query(modem);
        purge_calls(modem);

        mrp_free(modem->modem_id);
        mrp_free(modem->name);
        mrp_free(modem->manufacturer);
        mrp_free(modem->model);
        mrp_free(modem->revision);
        mrp_free(modem->serial);
        mrp_free(modem->type);

        if(modem->features)
            for(i = 0; modem->features[i] != NULL ; i++)
                mrp_free(modem->features[i]);

        if(modem->interfaces)
            for(i = 0; modem->interfaces[i] != NULL; i++)
                mrp_free(modem->interfaces[i]);

        mrp_free(modem);
        return TRUE;
    }
    return FALSE;
}


static void purge_modems(ofono_t *ofono)
{
    mrp_list_hook_t *p, *n;
    ofono_modem_t  *modem;

    FAIL_IF_NULL(ofono, ,"ofono proxy is NULL");
    FAIL_IF_NULL(ofono->dbus, ,"ofono->dbus is NULL");

    if (ofono->modem_qry != 0) {
        mrp_dbus_call_cancel(ofono->dbus, ofono->modem_qry);
        ofono->modem_qry = 0;
    }

    mrp_list_foreach(&ofono->modems, p, n) {
        modem = mrp_list_entry(p, ofono_modem_t, hook);
        free_modem(modem);
    }
}


static ofono_modem_t *create_modem(ofono_t *ofono, const char *path)
{
    ofono_modem_t   *modem;

    if (MRP_LIKELY((modem = mrp_allocz(sizeof(*modem))) != NULL)) {
        mrp_list_init(&modem->hook);
        mrp_list_init(&modem->calls);
        modem->ofono = ofono;

        if (MRP_LIKELY((modem->modem_id = mrp_strdup(path)) != NULL)) {
            mrp_list_append(&ofono->modems, &modem->hook);
            return modem;
        }

        free_modem(modem);
    }
    return NULL;
}


/*******************************************************************************
 * ofono modem handling
 */

static ofono_modem_t *find_modem(ofono_t *ofono, const char *path)
{
    mrp_list_hook_t *p, *n;
    ofono_modem_t  *modem;

    mrp_list_foreach(&ofono->modems, p, n) {
        modem = mrp_list_entry(p, ofono_modem_t, hook);
        if (modem->modem_id != NULL && !strcmp(modem->modem_id, path))
            FAIL_IF(modem->ofono != ofono, NULL, "corrupted modem data");
            return modem;
    }

    return NULL;
}


#if 0 /* not needed? */
static int modem_has_interface(ofono_modem_t *modem, const char *interface)
{
    mrp_debug("checking interface %s on modem %s, with interfaces",
              interface, modem->modem_id, modem->interfaces);

    return strarr_contains(modem->interfaces, interface);
}

static int modem_has_feature(ofono_modem_t *modem, const char *feature)
{
    mrp_debug("checking feature %s on modem %s, with features",
              feature, modem->modem_id, modem->features);

    return strarr_contains(modem->features, feature);
}


ofono_modem_t *ofono_online_modem(ofono_t *ofono)
{
    ofono_modem_t  *modem;
    mrp_list_hook_t *p, *n;

    mrp_list_foreach(&ofono->modems, p, n) {
        modem = mrp_list_entry(p, ofono_modem_t, hook);

        if (modem->powered && modem->online) {
            return modem;
        }
    }
    return NULL;
}
#endif


static void *parse_modem_property(mrp_dbus_msg_t *msg, void *target)
{
    ofono_modem_t   *modem = (ofono_modem_t *)target;

    /* The properties of interest of the DBUS message to be parsed. */
    mrp_dbus_dict_spec_t spec [] = {
        { "Type",         MRP_DBUS_TYPE_STRING,  &modem->type         },
        { "Powered",      MRP_DBUS_TYPE_BOOLEAN, &modem->powered      },
        { "Online",       MRP_DBUS_TYPE_BOOLEAN, &modem->online       },
        { "Lockdown",     MRP_DBUS_TYPE_BOOLEAN, &modem->lockdown     },
        { "Emergency",    MRP_DBUS_TYPE_BOOLEAN, &modem->emergency    },
        { "Name",         MRP_DBUS_TYPE_STRING,  &modem->name         },
        { "Manufacturer", MRP_DBUS_TYPE_STRING,  &modem->manufacturer },
        { "Model",        MRP_DBUS_TYPE_STRING,  &modem->model        },
        { "Revision",     MRP_DBUS_TYPE_STRING,  &modem->revision     },
        { "Serial",       MRP_DBUS_TYPE_STRING,  &modem->serial       },
        { "Interfaces",   MRP_DBUS_TYPE_ARRAY,   &modem->interfaces   },
        { "Features",     MRP_DBUS_TYPE_ARRAY,   &modem->features     }
        /* other properties are ignored */
    };

    int size = 12;

    /*
     * We are either called from modem_added_cb(), or modem_changed_cb().
     * In the former case we get an iterator pointing to a dictionary entry,
     * and in the latter an iterator pointing right to the property key.
     */

    if (mrp_dbus_msg_arg_type(msg, NULL) == MRP_DBUS_TYPE_DICT_ENTRY) {
        FAIL_IF(!parse_dbus_dict_entry(msg, spec, size), NULL, \
                "failed to parse dictionary");
    } else {
        FAIL_IF(!parse_dbus_dict_entry_tuple(msg, spec, size), NULL, \
                "failed to parse dictionary");
    }

    return modem;
}


static void *parse_modem_msg(mrp_dbus_msg_t *msg, ofono_t * ofono)
{
    char          *path;
    ofono_modem_t *modem = NULL;

    if(parse_dbus_object_path(msg, &path))
        if((modem = create_modem(ofono, path)) != NULL)
            if(!parse_dbus_dict(msg, parse_modem_property, modem)) {
                free_modem(modem);
                modem = NULL;
            }
    return modem;
}


static void *parse_modem_listed(mrp_dbus_msg_t *msg, void *user_data)
{
    ofono_t         *ofono;
    ofono_modem_t   *modem = NULL;

    mrp_debug("parsing ofono modem");
    ofono = (ofono_t *)user_data;
    FAIL_IF_NULL(ofono, FALSE, "ofono is NULL");

    /*
     * We are called from an initial modem_query_cb() with msg pointed to an
     * array{object, dict}, where dict is array{property, value}
     */

    if(mrp_dbus_msg_arg_type(msg, NULL) == MRP_DBUS_TYPE_STRUCT) {
        mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_STRUCT, NULL);
        modem = parse_modem_msg(msg, ofono);
        mrp_dbus_msg_exit_container(msg);
    }

    return modem;
}


static void cancel_modem_query(ofono_t *ofono)
{
    FAIL_IF_NULL(ofono, , "cancel_modem_query called on NULL");

    if (ofono->dbus != NULL && ofono->modem_qry)
        mrp_dbus_call_cancel(ofono->dbus, ofono->modem_qry);

    ofono->modem_qry = 0;
}


static void modem_query_cb(mrp_dbus_t *dbus,
                           mrp_dbus_msg_t *msg,
                           void *user_data)
{
    ofono_t         *ofono = (ofono_t *)user_data;
    ofono_modem_t   *modem;
    mrp_list_hook_t  *p, *n;
    (void)dbus;

    mrp_debug("modem query response on oFono");
    FAIL_IF_NULL(ofono, , "modem_query_cb called on NULL ofono proxy");

    /* parse an array of dictionaries */
    if (!dbus_array_foreach(msg, parse_modem_listed, ofono)) {
        mrp_log_error("failed to process modem query response");
        return;
    }

    /* all modems have been parsed into the list, now query the calls */
    mrp_list_foreach(&ofono->modems, p, n) {
        modem = mrp_list_entry(p, ofono_modem_t, hook);
        dump_modem(modem);

        query_calls(dbus, modem);
    }
}


static int query_modems(ofono_t *ofono)
{
    mrp_debug("querying modems on oFono");
    cancel_modem_query(ofono);

    ofono->modem_qry = mrp_dbus_call(ofono->dbus, OFONO_SERVICE, "/",
                                     OFONO_MODEM_MGR, "GetModems", 5000,
                                     modem_query_cb, ofono,
                                     MRP_DBUS_TYPE_INVALID);

    return (ofono->modem_qry != 0 ? TRUE : FALSE);
}


static int modem_removed_cb(mrp_dbus_t *dbus,
                            mrp_dbus_msg_t *msg,
                            void *user_data)
{
    ofono_t       *ofono;
    const char    *path;
    ofono_modem_t *modem;
    (void)dbus;

    ofono  = (ofono_t *)user_data;
    path = mrp_dbus_msg_path(msg);
    modem = find_modem(ofono, path);

    if(modem != NULL) {
        mrp_debug("modem '%s' was removed", path);
        return free_modem(modem);
    }

    return FALSE;
}


static int modem_added_cb(mrp_dbus_t *dbus,
                          mrp_dbus_msg_t *msg,
                          void *user_data)
{
    ofono_t         *ofono = (ofono_t *)user_data;
    ofono_modem_t   *modem;
    (void)dbus;

    mrp_debug("adding new oFono modem...");
    FAIL_IF_NULL(ofono, FALSE, "ofono is NULL");

    if((modem = parse_modem_msg(msg, ofono)) != NULL)
        return query_calls(dbus, modem);

    mrp_debug("failed to parse added modem");
    return FALSE;
}


static int modem_changed_cb(mrp_dbus_t *dbus,
                            mrp_dbus_msg_t *msg,
                            void *user_data)
{
    ofono_t         *ofono;
    const char      *path;
    ofono_modem_t   *modem;
    (void)dbus;

    ofono = (ofono_t *)user_data;
    path = mrp_dbus_msg_path(msg);
    modem = find_modem(ofono, path);

    if (modem != NULL) {
        mrp_debug("changes in modem '%s'...", modem->modem_id);

        if (parse_modem_property(msg, modem)) {
            dump_modem(modem);
            /* placeholder to handle Online, Powered, Lockdown signals */
            /* since call objects will be signaled separately, do nothing */
        }
    }

    return TRUE;
}


/******************************************************************************
 * call objects' lifecycle
 */

static void dump_call(ofono_call_t *call)
{

    if (call == NULL) {
        mrp_debug("    none");
        return;
    }

    mrp_debug("call '%s' {", call->call_id);
    mrp_debug("    service_id:           '%s'", DUMP_STR(call->service_id));
    mrp_debug("    line_id:              '%s'", DUMP_STR(call->line_id));
    mrp_debug("    name:                 '%s'", DUMP_STR(call->name));
    mrp_debug("    state:                '%s'", DUMP_STR(call->state));
    mrp_debug("    end_reason:           '%s'", DUMP_STR(call->end_reason));
    mrp_debug("    start_time:           '%s'", DUMP_STR(call->start_time));
    mrp_debug("    is multiparty:        '%s'", DUMP_YESNO(call->multiparty));
    mrp_debug("    is emergency:         '%s'", DUMP_YESNO(call->emergency));
    mrp_debug("    information:          '%s'", DUMP_STR(call->info));
    mrp_debug("    icon_id:              '%u'", call->icon_id);
    mrp_debug("    remote held:          '%s'", DUMP_YESNO(call->remoteheld));
    mrp_debug("}");
}


static void free_call(ofono_call_t *call)
{
    if (call) {
        mrp_list_delete(&call->hook);
        mrp_free(call->call_id);
        mrp_free(call->service_id);
        mrp_free(call->line_id);
        mrp_free(call->name);
        mrp_free(call->state);
        mrp_free(call->end_reason);
        mrp_free(call->start_time);
        mrp_free(call->info);

        mrp_free(call);
    }
}


/**
 * find service_id (modem) from call path (call_id)
 * example: path = "/hfp/00DBDF143ADC_44C05C71BAF6/voicecall01",
 * modem_id = "/hfp/00DBDF143ADC_44C05C71BAF6"
 * call_id = path
 */
static int get_modem_id_from_call_path(char *call_path, char **modem_id)
{
    char * dest = NULL;
    unsigned int i = (call_path == NULL ? 0 : strlen(call_path)-1);

    for(; i > 0 && call_path[i] != '/'; i--);
    if(i > 0) {
        call_path[i] = '\0';
        dest = mrp_strdup(call_path);
        call_path[i] = '/'; /* restore path */
    }
    *modem_id = dest;
    return (i > 0 ? TRUE : FALSE);
}


static ofono_call_t *create_call(ofono_modem_t *modem, const char *path)
{
    ofono_call_t   *call;

    if (MRP_LIKELY((call = mrp_allocz(sizeof(*call))) != NULL)) {

        mrp_list_init(&call->hook);
        call->modem = modem;

        if (MRP_LIKELY((call->call_id = mrp_strdup(path)) != NULL))
            if(get_modem_id_from_call_path((char*) path, &(call->service_id))) {
                mrp_list_append(&modem->calls, &call->hook);
                return call;
            }

        free_call(call);
    }
    return NULL;
}


static void purge_calls(ofono_modem_t *modem)
{
    mrp_list_hook_t *p, *n;
    ofono_call_t  *call;
    ofono_t * ofono;

    if (modem) {
        ofono = modem->ofono;
        if (ofono->dbus) {
            if (modem->call_qry != 0) {
                mrp_dbus_call_cancel(ofono->dbus, modem->call_qry);
                modem->call_qry = 0;
            }

            mrp_list_foreach(&modem->calls, p, n) {
                call = mrp_list_entry(p, ofono_call_t, hook);
                ofono->notify(TEL_CALL_REMOVED, (tel_call_t *)call, modem);
                free_call(call);
            }
        }
    }
}


/*******************************************************************************
 * ofono call handling
 */

static ofono_call_t *find_call(ofono_modem_t *modem, const char *path)
{
    mrp_list_hook_t *p, *n;
    ofono_call_t  *call;

    mrp_list_foreach(&modem->calls, p, n) {
        call = mrp_list_entry(p, ofono_call_t, hook);

        if (call->call_id != NULL && !strcmp(call->call_id, path))
            return call;
    }

    return NULL;
}


static void *parse_call_property(mrp_dbus_msg_t *msg, void *target)
{
    ofono_call_t    *call = (ofono_call_t *) target;

    /** The properties of interest of the DBUS message to be parsed.
     * Beware that if ofono defines a new property, this has to be updated,
     * otherwise parsing will fail if that property is present.
     */
    mrp_dbus_dict_spec_t spec [] = {
        { "LineIdentification", MRP_DBUS_TYPE_STRING,  &call->line_id       },
        { "Name",               MRP_DBUS_TYPE_STRING,  &call->name          },
        { "Multiparty",         MRP_DBUS_TYPE_BOOLEAN, &call->multiparty    },
        { "State",              MRP_DBUS_TYPE_STRING,  &call->state         },
        { "Emergency",          MRP_DBUS_TYPE_BOOLEAN, &call->emergency     },
        { "IncomingLine",       MRP_DBUS_TYPE_STRING,  &call->incoming_line },
        { "StartTime",          MRP_DBUS_TYPE_STRING,  &call->start_time    },
        { "Information",        MRP_DBUS_TYPE_STRING,  &call->info          },
        { "Icon",               MRP_DBUS_TYPE_BYTE,    &call->icon_id       },
        { "RemoteHeld",         MRP_DBUS_TYPE_BOOLEAN, &call->remoteheld    },
        { "RemoteMultiparty",   MRP_DBUS_TYPE_BOOLEAN, &call->remote_mpy    }
    /* other properties are ignored */
    };

    int size = 11;

    /*
     * We are either called from call_added_cb(), or call_changed_cb().
     * In the former case we get an iterator pointing to a dictionary entry,
     * and in the latter an iterator pointing right to the property key.
     */

    if (mrp_dbus_msg_arg_type(msg, NULL) == MRP_DBUS_TYPE_DICT_ENTRY) {
        FAIL_IF(!parse_dbus_dict_entry(msg, spec, size), NULL, \
                "failed to parse dictionary");
    } else {
        FAIL_IF(!parse_dbus_dict_entry_tuple(msg, spec, size), NULL, \
                "failed to parse dictionary");
    }

    return call;
}


static void *parse_call_msg(mrp_dbus_msg_t *msg, ofono_modem_t * modem)
{
    char         *path;
    ofono_call_t *call = NULL;

    if(parse_dbus_object_path(msg, &path))
        if((call = create_call(modem, path)) != NULL)
            if(!parse_dbus_dict(msg, parse_call_property, call)) {
                mrp_debug("failed to parse call on modem %s", modem->modem_id);
                free_call(call);
                call = NULL;
            }
    return call;
}


static void *parse_call_listed(mrp_dbus_msg_t *msg, void * user_data)
{
    ofono_call_t * call = NULL;
    ofono_modem_t * modem = (ofono_modem_t *) user_data;

    mrp_debug("parsing listed calls in modem '%s'...", modem->modem_id);

    /* Called with an array of {object, dictionary} */
    if (mrp_dbus_msg_arg_type(msg, NULL) == MRP_DBUS_TYPE_STRUCT) {
        mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_STRUCT, NULL);
        call = parse_call_msg(msg, modem);
        mrp_dbus_msg_exit_container(msg);
    }
    return call;
}


static int call_changed_cb(mrp_dbus_t *dbus,
                           mrp_dbus_msg_t *msg,
                           void *user_data)
{
    char            *path, *modem_id;
    ofono_t         *ofono;
    ofono_modem_t   *modem;
    ofono_call_t    *call;
    (void)dbus;

    ofono = (ofono_t *)user_data;
    FAIL_IF_NULL(ofono, FALSE, "ofono is NULL");

    path = (char *) mrp_dbus_msg_path(msg); /* contains call object path */
    get_modem_id_from_call_path(path, &modem_id);
    modem = find_modem(ofono, modem_id);        /* modem path is modem_id    */
    FAIL_IF_NULL(modem, FALSE, "modem is NULL");
    FAIL_IF(modem->ofono != ofono, FALSE, "corrupted modem data");

    call = find_call(modem, path);
    FAIL_IF_NULL(call, FALSE, "call not found based on path %s", path);

    mrp_debug("changes in call '%s'...", path);

    FAIL_IF(!parse_call_property(msg, call), FALSE,
            "parsing error in call change callback for %s", call->call_id);

    FAIL_IF_NULL(ofono->notify, FALSE, "notify is NULL");

    mrp_debug("calling notify TEL_CALL_CHANGED on call %s, modem %s",
              (tel_call_t*)call->call_id, modem->modem_id);

#if _NOTIFY_MDB
    ofono->notify(TEL_CALL_CHANGED, (tel_call_t*)call, modem->modem_id);
#else
    dump_call((ofono_call_t*)call);
#endif
    mrp_debug("oFono call changed: %s", call->call_id);
    /*dump_modem(modem);*/
    return TRUE;
}


static int call_endreason_cb(mrp_dbus_t *dbus,
                             mrp_dbus_msg_t *msg,
                             void *user_data)
{
    ofono_t         *ofono;
    ofono_modem_t   *modem;
    ofono_call_t    *call;
    char            *modem_id;
    char            *path = (char *) mrp_dbus_msg_path(msg);
    (void)dbus;

    ofono = (ofono_t *)user_data;
    FAIL_IF_NULL(ofono, FALSE, "ofono is NULL");

    FAIL_IF(!get_modem_id_from_call_path(path, &modem_id), FALSE,
            "failed to get modem id from call path %s", path);

    modem = find_modem(ofono, modem_id);
    FAIL_IF_NULL(modem, FALSE, "modem is not found for id %s", modem_id);

    call = find_call(modem, path);
    FAIL_IF_NULL(call, FALSE, "call not found based on path %s", path);

    FAIL_IF(!parse_dbus_string(msg, &path), FALSE, \
            "expected end reason as string for call %s", call->call_id);

    call->end_reason = mrp_strdup(path ? path : "");

    mrp_debug("disconnect reason in call '%s': %s",
              call->call_id, call->end_reason);

    FAIL_IF_NULL(ofono->notify, FALSE, "notify is NULL");
    mrp_debug("calling notify TEL_CALL_CHANGED on call %s, modem %s",
              (tel_call_t*)call->call_id, modem->modem_id);
#if _NOTIFY_MDB
    ofono->notify(TEL_CALL_CHANGED, (tel_call_t*)call, modem->modem_id);
#else
    dump_call((ofono_call_t*)call);
#endif
    mrp_debug("oFono call end reason changed: %s", call->call_id);
    /*dump_modem(modem);*/
    return TRUE;
}


/******************************************************************************
 * ofono call manager
 */

static void cancel_call_query(ofono_modem_t *modem)
{
    FAIL_IF_NULL(modem, ,"modem is NULL");
    FAIL_IF_NULL(modem->ofono, ,"ofono is NULL in modem %s", modem->modem_id);

    if (modem->ofono->dbus && modem->call_qry != 0)
        mrp_dbus_call_cancel(modem->ofono->dbus, modem->call_qry);

    modem->call_qry = 0;
}


static void call_query_cb(mrp_dbus_t *dbus,
                          mrp_dbus_msg_t *msg,
                          void *user_data)
{
    ofono_call_t    *call;
    mrp_list_hook_t *p, *n;

    ofono_modem_t   *modem = (ofono_modem_t *)user_data;
    FAIL_IF_NULL(modem, , "modem is NULL");

    ofono_t         *ofono = modem->ofono;
    FAIL_IF_NULL(ofono, , "ofono is NULL");

    (void)dbus;

    mrp_debug("call query callback on modem %s", modem->modem_id);

    modem->call_qry = 0;

    if (!dbus_array_foreach(msg, parse_call_listed, modem)) {
        mrp_log_error("failed processing call query response");
        return;
    }

    dump_modem(modem);

    mrp_debug("calling notify TEL_CALL_PURGE on modem %s", modem->modem_id);
    /*modem->ofono->notify(TEL_CALL_PURGE, NULL, modem->modem_id);*/
    mrp_list_foreach(&modem->calls, p, n) {
        call = mrp_list_entry(p, ofono_call_t, hook);
        mrp_debug("calling notify TEL_CALL_LISTED on call %s, modem %s",
                  (tel_call_t*)call->call_id, modem->modem_id);
#if _NOTIFY_MDB
            ofono->notify(TEL_CALL_LISTED, (tel_call_t*)call, modem->modem_id);
#else
            dump_call((ofono_call_t*)call);
#endif
            mrp_debug("new oFono call listed: %s", call->call_id);
    }
}


static int call_removed_cb(mrp_dbus_t *dbus,
                           mrp_dbus_msg_t *msg,
                           void *user_data)
{
    ofono_t         *ofono;
    ofono_modem_t   *modem;
    ofono_call_t    *call;
    char            *call_path;
    char            *path = (char *) mrp_dbus_msg_path(msg);
    (void)dbus;

    ofono = (ofono_t *)user_data;
    FAIL_IF_NULL(ofono, FALSE, "ofono is NULL");

    modem = find_modem(ofono, path);
    FAIL_IF_NULL(modem, FALSE, "modem is not found based on path %s", path);

    FAIL_IF(!parse_dbus_object_path(msg, &call_path), FALSE, \
            "could not parse removed call object path");

    mrp_debug("call '%s' signaled to be removed", call_path);
    call = find_call(modem, call_path);
    FAIL_IF_NULL(call, FALSE, \
                 "could not find call based on path %s", call_path);

    FAIL_IF_NULL(ofono->notify, FALSE, "notify is NULL");

    mrp_debug("calling notify TEL_CALL_REMOVED on call %s, modem %s",
               (tel_call_t*)call->call_id, modem->modem_id);
#if _NOTIFY_MDB
    ofono->notify(TEL_CALL_REMOVED, (tel_call_t*)call, modem->modem_id);
#else
    dump_call((ofono_call_t*)call);
#endif
    mrp_debug("oFono call removed: %s", call->call_id);
    free_call(call);
    /*dump_modem(modem);*/
    return TRUE;
}


static int call_added_cb(mrp_dbus_t *dbus, mrp_dbus_msg_t *msg, void *user_data)
{
    ofono_t         *ofono;
    ofono_modem_t   *modem;
    ofono_call_t    *call;
    const char      *path;
    (void)dbus;

    ofono = (ofono_t *)user_data;
    FAIL_IF_NULL(ofono, FALSE, "ofono is NULL");

    path = mrp_dbus_msg_path(msg);

    if((modem = find_modem(ofono, path)) != NULL) {
        mrp_debug("new oFono call signaled on modem %s", modem->modem_id);

        if((call = parse_call_msg(msg, modem)) != NULL)
            if(ofono->notify) {
                mrp_debug("calling notify TEL_CALL_ADDED on call %s, modem %s",
                          (tel_call_t*)call->call_id, modem->modem_id);
            #if _NOTIFY_MDB
                ofono->notify(TEL_CALL_ADDED,
                              (tel_call_t*)call,
                              modem->modem_id);
            #else
                dump_call((ofono_call_t*)call);
            #endif
                mrp_debug("new oFono call added: %s", call->call_id);
                /*dump_modem(modem);*/
                return TRUE;
            }
    }

    mrp_debug("could not find modem based on path %s", path);
    return FALSE;
}


static int query_calls(mrp_dbus_t *dbus, ofono_modem_t *modem)
{
    FAIL_IF_NULL(modem, FALSE, "modem is NULL");

    if (modem->call_qry != 0)
        return TRUE;    /* already querying */

    if (modem->online && strarr_contains(modem->interfaces, OFONO_CALL_MGR)) {
        modem->call_qry  = mrp_dbus_call(dbus, OFONO_SERVICE, modem->modem_id,
                                         OFONO_CALL_MGR, "GetCalls", 5000,
                                         call_query_cb, modem,
                                         MRP_DBUS_TYPE_INVALID);

        return modem->call_qry != 0;
    } else
        cancel_call_query(modem);
        mrp_debug("call query canceled on modem %s: offline or no callmanager",
                  modem->modem_id);


    return FALSE;
}
