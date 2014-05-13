#include <stdlib.h>
#include <syslog.h>

#include <murphy/common.h>
#include <murphy/core.h>
#include <murphy/core/domain.h>
#include <murphy/resource/client-api.h>

typedef struct gamctl_s gamctl_t;

static int  resctl_init(gamctl_t *gam);
static void resctl_exit(gamctl_t *gam);
static int  domctl_init(gamctl_t *gam);
static void domctl_exit(gamctl_t *gam);
static void gamctl_exit(mrp_plugin_t *plugin);

static int gamctl_route_cb(int narg, mrp_domctl_arg_t *args,
                           uint32_t *nout, mrp_domctl_arg_t *outs,
                           void *user_data);

/*
 * plugin context/state
 */

struct gamctl_s {
    mrp_plugin_t          *self;              /* us, this plugin */
    mrp_resource_client_t *rsc;               /* resource client */
    uint32_t               seq;               /* request sequence number */
};


static int resctl_init(gamctl_t *gam)
{
    gam->seq = 1;
    gam->rsc = mrp_resource_client_create("genivi-audio-manager", gam);

    if (gam->rsc == NULL) {
        mrp_log_error("Failed to create Genivi Audio Manager resource client.");
        return FALSE;
    }
    else {
        mrp_log_info("Created Genivi Audio Manager resource client.");
        return TRUE;
    }
}


static void resctl_event_cb(uint32_t seq, mrp_resource_set_t *set,
                            void *user_data)
{
    gamctl_t *gam = (gamctl_t *)user_data;

    MRP_UNUSED(seq);
    MRP_UNUSED(set);
    MRP_UNUSED(gam);
}


static int resctl_create(gamctl_t *gam, uint16_t source, uint16_t sink,
                         uint16_t connid, uint16_t connno)
{
    mrp_resource_client_t *rsc    = gam->rsc;
    const char            *zone   = "driver";
    const char            *cls    = "player";
    const char            *play   = "audio_playback";
    const char            *rec    = "audio_recording";
    uint32_t               prio   = 0;
    bool                   ar     = false;
    bool                   nowait = false;
    mrp_resource_set_t    *set;
    mrp_attr_t             attrs[16], *attrp;;

    mrp_log_info("Creating and acquiring resource set for Genivi Audio Manager "
                 "connection %u (%u -> %u, #%u).", connid, source, sink,
                 connno);

    set = mrp_resource_set_create(rsc, ar, nowait, prio, resctl_event_cb, gam);

    if (set == NULL) {
        mrp_log_error("Failed to create resource set for "
                      "Genivi Audio Manager connection %u (%u -> %u).",
                      connid, source, sink);
        goto fail;
    }


    attrs[0].type          = mqi_integer;
    attrs[0].name          = "source_id";
    attrs[0].value.integer = source;
    attrs[1].type          = mqi_integer;
    attrs[1].name          = "sink_id";
    attrs[1].value.integer = sink;
    attrs[2].type          = mqi_integer;
    attrs[2].name          = "connid";
    attrs[2].value.integer = connid;
    attrs[3].type          = mqi_integer;
    attrs[3].name          = "connno";
    attrs[3].value.integer = connno;
    attrs[4].name          = NULL;

    attrp = &attrs[0];

    /*
     * XXX TODO: adding audio_recording should be done only for connections
     * from/to certain sources/sinks
     */
    if (mrp_resource_set_add_resource(set, play, false, attrp, true) < 0 ||
        mrp_resource_set_add_resource(set, rec , false, attrp, true) < 0) {
        mrp_log_error("Failed to add resource (%s or %s) to Genivi Audio "
                      "Manager resource set.", play, rec);
        goto fail;
    }

    if (mrp_application_class_add_resource_set(cls, zone, set, 0) != 0) {
        mrp_log_error("Failed to add Genivi Audio Manager resource set "
                      "to application class %s in zone %s.", cls, zone);
        goto fail;
    }

    mrp_resource_set_acquire(set, gam->seq++);

    return TRUE;

 fail:
    if (set != NULL)
        mrp_resource_set_destroy(set);

    return FALSE;
}


static void resctl_exit(gamctl_t *gam)
{
    if (gam != NULL && gam->rsc != NULL) {
        mrp_log_info("Destroying Genivi Audio Manager resource client.");
        mrp_resource_client_destroy(gam->rsc);
        gam->rsc = NULL;
    }
}


static int domctl_init(gamctl_t *gam)
{
    mrp_context_t           *ctx           = gam->self->ctx;
    mrp_domain_method_def_t  gam_methods[] = {
        { "request_route", 32, gamctl_route_cb, gam },
    };
    size_t                   gam_nmethod   = MRP_ARRAY_SIZE(gam_methods);

    if (mrp_register_domain_methods(ctx, gam_methods, gam_nmethod)) {
        mrp_log_info("Registered Genivi Audio Manager domain methods.");
        return TRUE;
    }
    else {
        mrp_log_error("Failed to register Genivi Audio Manager domain methods.");
        return FALSE;
    }
}


static void domctl_exit(gamctl_t *gam)
{
    MRP_UNUSED(gam);
}


static int gamctl_route_cb(int narg, mrp_domctl_arg_t *args,
                           uint32_t *nout, mrp_domctl_arg_t *outs,
                           void *user_data)
{
    gamctl_t     *gam = (gamctl_t *)user_data;
    const char   *error;
    uint16_t      source, sink, *path, *route;
    size_t        len, i;

    if (narg < 2) {
        error = "invalid number of arguments (expecting >= 2)";
        goto error;
    }

    if (args[0].type != MRP_DOMCTL_ARRAY(UINT16) ||
        args[1].type != MRP_DOMCTL_ARRAY(UINT16)) {
        error = "invalid argument types (expecting arrays of uint16_t)";
        goto error;
    }

    if (args[0].size != 2) {
        error = "invalid route request (expecting array of 2 items)";
        goto error;
    }

    mrp_log_info("Got routing request for %u -> %u with %d possible routes.",
                 ((uint16_t *)args[0].arr)[0], ((uint16_t *)args[0].arr)[1],
                 narg - 1);

    source = ((uint16_t *)args[0].arr)[0];
    sink   = ((uint16_t *)args[0].arr)[1];
    route  = args[1].arr;
    len    = args[1].size;

    if (len == 0) {
        error = "invalid route (empty)";
        goto error;
    }

    resctl_create(gam, source, sink, -1, 0);

    path = mrp_allocz(len * sizeof(path[0]));

    if (path == NULL) {
        error = NULL;
        goto error;
    }

    for (i = 0; i < len; i++)
        path[i] = route[i];

    *nout = 1;
    outs[0].type = MRP_DOMCTL_ARRAY(UINT16);
    outs[0].arr  = path;
    outs[0].size = len;

    return 0;

 error:
    *nout = 1;
    outs[0].type = MRP_DOMCTL_STRING;
    outs[0].str  = mrp_strdup(error);

    return -1;
}


static int gamctl_init(mrp_plugin_t *plugin)
{
    gamctl_t *gam;

    gam = mrp_allocz(sizeof(*gam));

    if (gam == NULL)
        goto fail;

    gam->self    = plugin;
    plugin->data = gam;

    if (!resctl_init(gam))
        goto fail;

    if (!domctl_init(gam))
        goto fail;

    return TRUE;

 fail:
    gamctl_exit(plugin);
    return FALSE;
}


static void gamctl_exit(mrp_plugin_t *plugin)
{
    gamctl_t *gam = plugin->data;

    resctl_exit(gam);
    domctl_exit(gam);
}


#define GAMCTL_DESCRIPTION "Genivi Audio Manager control plugin for Murphy"
#define GAMCTL_HELP        "Genivi Audio Manager control plugin for Murphy."
#define GAMCTL_VERSION     MRP_VERSION_INT(0, 0, 1)
#define GAMCTL_AUTHORS     "Krisztian Litkey <kli@iki.fi>"

MURPHY_REGISTER_PLUGIN("gam-control",
                       GAMCTL_VERSION, GAMCTL_DESCRIPTION,
                       GAMCTL_AUTHORS, GAMCTL_HELP, MRP_SINGLETON,
                       gamctl_init, gamctl_exit,
                       NULL, 0, NULL, 0, NULL, 0, NULL);
