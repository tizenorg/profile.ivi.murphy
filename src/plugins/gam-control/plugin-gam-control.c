#include <stdlib.h>
#include <syslog.h>

#include <murphy/common.h>
#include <murphy/core.h>
#include <murphy/core/domain.h>
#include <murphy/resource/client-api.h>
#include <murphy/resource/manager-api.h>

#include <murphy-db/mqi.h>
#include <murphy-db/mql.h>
#include <murphy-db/mql-result.h>

#define SOURCETBL "audio_manager_sources"
#define SINKTBL   "audio_manager_sinks"

typedef struct gamctl_s gamctl_t;
typedef struct route_s  route_t;


/*
 * plugin context/state
 */

struct gamctl_s {
    mrp_plugin_t          *self;         /* us, this plugin */
    mrp_resource_client_t *rsc;          /* resource client */
    mrp_htbl_t            *rstbl;        /* resource sets */
    uint32_t               seq;          /* request sequence number */
    uint32_t               sourcef;      /* source table fingerprint (stamp) */
    uint32_t               sinkf;        /* sink table fingerprint (stamp) */
    route_t               *routes;       /* routing table */
    size_t                 nroute;       /* number of routing entries */
    mrp_deferred_t        *recalc;       /* deferred resource recalculation */
};


/*
 * a route from source to sink
 */

struct route_s {
    char     *source;                    /* source name */
    char     *sink;                      /* sink name */
    uint16_t *hops;                      /* routing hops */
    size_t    nhop;                      /* number of hops */
};


/*
 * a source or sink node to id mapping
 */

typedef struct {
    const char *name;                    /* source/sink name */
    uint16_t    id;                      /* source/sink id */
} node_t;


/*
 * a (predefined) routing path
 */

typedef struct {
    size_t  nhop;                        /* number of hops */
    char   *hops[32];                    /* hop names */
} path_t;



static int  resctl_init(gamctl_t *gam);
static void resctl_exit(gamctl_t *gam);
static int  domctl_init(gamctl_t *gam);
static void domctl_exit(gamctl_t *gam);
static void gamctl_exit(mrp_plugin_t *plugin);

static int gamctl_route_cb(int narg, mrp_domctl_arg_t *args,
                           uint32_t *nout, mrp_domctl_arg_t *outs,
                           void *user_data);
static int gamctl_disconnect_cb(int narg, mrp_domctl_arg_t *args,
                                uint32_t *nout, mrp_domctl_arg_t *outs,
                                void *user_data);

static char *route_dump(gamctl_t *gam, char *buf, size_t size,
                        route_t *r, int verbose);


/*
 * hardwired sources, sinks and routing paths
 */

static node_t sources[] = {
    { "wrtApplication", 0 },
    { "icoApplication", 0 },
    { "phoneSource"   , 0 },
    { "radio"         , 0 },
    { "microphone"    , 0 },
    { "navigator"     , 0 },
    { "gw1Source"     , 0 },
    { "gw2Source"     , 0 },
    { "gw3Source"     , 0 },
    { "gw4Source"     , 0 },
    { "gw5Source"     , 0 },
    { "gw6Source"     , 0 },
    { NULL, 0 }
};

static node_t sinks[] = {
    { "btHeadset"       , 0 },
    { "usbHeadset"      , 0 },
    { "speakers"        , 0 },
    { "wiredHeadset"    , 0 },
    { "phoneSink"       , 0 },
    { "voiceRecognition", 0 },
    { "gw1Sink"         , 0 },
    { "gw2Sink"         , 0 },
    { "gw3Sink"         , 0 },
    { "gw4Sink"         , 0 },
    { "gw5Sink"         , 0 },
    { "gw6Sink"         , 0 },
    { NULL, 0 }
};

static path_t paths[] = {
    { 2, { "wrtApplication", "btHeadset"  } },
    { 2, { "wrtApplication", "usbHeadset" } },
    { 4, { "wrtApplication", "gw2Sink", "gw2Source", "speakers"     } },
    { 4, { "wrtApplication", "gw2Sink", "gw2Source", "wiredHeadset" } },

    { 2, { "icoApplication", "btHeadset"  } },
    { 2, { "icoApplication", "usbHeadset" } },
    { 4, { "icoApplication", "gw1Sink", "gw1Source", "speakers"     } },
    { 4, { "icoApplication", "gw1Sink", "gw1Source", "wiredHeadset" } },

    { 2, { "phoneSource", "btHeadset"  } },
    { 2, { "phoneSource", "usbHeadset" } },
    { 4, { "phoneSource", "gw1Sink", "gw1Source", "speakers"     } },
    { 4, { "phoneSource", "gw1Sink", "gw1Source", "wiredHeadset" } },

    { 4, { "radio", "gw3Sink", "gw3Source", "btHeadset"  } },
    { 4, { "radio", "gw3Sink", "gw3Source", "usbHeadset" } },
    { 2, { "radio", "speakers"     } },
    { 2, { "radio", "wiredHeadset" } },

    { 4, { "microphone", "gw6Sink", "gw6Source", "phoneSink"        } },
    { 4, { "microphone", "gw5Sink", "gw5Source", "voiceRecognition" } },

    { 4, { "navigator", "gw4Sink", "gw4Source", "speakers"     } },
    { 4, { "navigator", "gw4Sink", "gw4Source", "wiredHeadset" } },
};


static uint16_t node_id(node_t *nodes, const char *name)
{
    node_t *node;

    for (node = nodes; node->name != NULL; node++)
        if (!strcmp(node->name, name))
            return node->id;

    return 0;
}


static uint16_t source_id(const char *name)
{
    return node_id(sources, name);
}


static uint16_t sink_id(const char *name)
{
    return node_id(sinks, name);
}


static int exec_mql(mql_result_type_t type, mql_result_t **resultp,
                    const char *format, ...)
{
    mql_result_t *r;
    char          buf[4096];
    va_list       ap;
    int           success, n;

    va_start(ap, format);
    n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (n < (int)sizeof(buf)) {
        r       = mql_exec_string(type, buf);
        success = (r == NULL || mql_result_is_success(r));

        if (resultp != NULL) {
            *resultp = r;
            return success;
        }
        else {
            mql_result_free(r);
            return success;
        }
    }
    else {
        if (resultp != NULL)
            *resultp = NULL;

        return FALSE;
    }
}


static void reset_nodes(node_t *nodes)
{
    node_t *node;

    for (node = nodes; node->name != NULL; node++)
        node->id = 0;
}


static int resolve_nodes(const char *type, node_t *nodes,
                         const char *tbl, uint32_t *age)
{
    mql_result_t *r = NULL;
    uint32_t      h, stamp;
    node_t       *node;
    uint16_t      nrow, i, id;
    const char   *name;

    if ((h = mqi_get_table_handle((char *)tbl)) == MQI_HANDLE_INVALID)
        return FALSE;

    if (*age >= (stamp = mqi_get_table_stamp(h)))
        return TRUE;

    if (!exec_mql(mql_result_rows, &r, "select id,name from %s", tbl))
        return FALSE;

    reset_nodes(nodes);

    if (r == NULL)
        return FALSE;

    nrow = mql_result_rows_get_row_count(r);

    if (nrow == 0)
        return FALSE;

    if (mql_result_rows_get_row_column_type(r, 0) != mqi_integer ||
        mql_result_rows_get_row_column_type(r, 1) != mqi_string) {
        mrp_log_error("Invalid column types for table '%s'.", tbl);
        return FALSE;
    }

    for (i = 0; i < nrow; i++) {
        id   = mql_result_rows_get_integer(r, 0, i);
        name = mql_result_rows_get_string(r, 1, i, NULL, 0);

        for (node = nodes; node->name != NULL; node++) {
            if (!strcmp(node->name, name)) {
                node->id = id;
                mrp_log_info("%s '%s' has now id %u", type, name, id);
                break;
            }
        }

        if (node->name == NULL)
            mrp_log_info("(unused) %s '%s' has id %u", type, name, id);
    }

    mql_result_free(r);

    *age = stamp;

    return TRUE;
}


static int resolve_routes(gamctl_t *gam)
{
    const char *type, *node;
    char        route[4096];
    route_t    *r;
    path_t     *p;
    uint16_t    id;
    size_t      i;
    int         incomplete;

    for (r = gam->routes, p = paths; r->nhop; r++, p++) {
        incomplete = FALSE;
        for (i = 0; i < r->nhop; i++) {
            node = p->hops[i];

            if (i & 0x1) {
                type = "sink";
                id   = sink_id(node);
            }
            else {
                type = "source";
                id   = source_id(node);
            }

            if (!id) {
                mrp_log_warning("Unresolved %s '%s'.",type, node);
                incomplete = TRUE;
            }

            r->hops[i] = id;
        }

        if (!incomplete)
            mrp_log_info("Resolved route: %s",
                         route_dump(gam, route, sizeof(route), r, TRUE));
        else
            mrp_log_warning("Unresolvable route: %s",
                            route_dump(gam, route, sizeof(route), r, TRUE));
    }

    return TRUE;
}


static int connid_cmp(const void *key1, const void *key2)
{
    return key2 - key1;
}


static uint32_t connid_hash(const void *key)
{
    uint16_t connid = (uint16_t)(ptrdiff_t)key;

    return connid;
}


static int resctl_init(gamctl_t *gam)
{
    mrp_htbl_config_t hcfg;

    mrp_clear(&hcfg);
    hcfg.comp  = connid_cmp;
    hcfg.hash  = connid_hash;
    hcfg.free  = NULL;

    gam->rstbl = mrp_htbl_create(&hcfg);

    if (gam->rstbl == NULL)
        return FALSE;

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


static mrp_resource_set_t *resctl_create(gamctl_t *gam, uint16_t source,
                                         uint16_t sink, uint16_t connid,
                                         uint16_t connno)
{
    mrp_resource_client_t *rsc    = gam->rsc;
    uint32_t               seq    = gam->seq++;
    const char            *zone   = "driver";
    const char            *cls    = "player";
    const char            *play   = "audio_playback";
    uint32_t               prio   = 0;
    bool                   ar     = false;
    bool                   nowait = false;
    mrp_resource_set_t    *set;
    mrp_attr_t             attrs[16], *attrp;

    mrp_log_info("Creating resource set for Genivi Audio Manager connection "
                 "%u (%u -> %u, #%u).", connid, source, sink, connno);

    set = mrp_resource_set_create(rsc, ar, nowait, prio, resctl_event_cb, gam);

    if (set == NULL) {
        mrp_log_error("Failed to create resource set for Genivi Audio Manager "
                      "connection %u (%u -> %u).", connid, source, sink);
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

    if (mrp_resource_set_add_resource(set, play, true, attrp, true) < 0) {
        mrp_log_error("Failed to add resource %s to Genivi Audio "
                      "Manager resource set.", play);
        goto fail;
    }

    if (mrp_application_class_add_resource_set(cls, zone, set, seq) != 0) {
        mrp_log_error("Failed to add Genivi Audio Manager resource set "
                      "to application class %s in zone %s.", cls, zone);
        goto fail;
    }

    if (mrp_htbl_insert(gam->rstbl, (void *)(ptrdiff_t)connid, set))
        return set;
    else
        mrp_log_error("Failed to associate resource set with connection %u.",
                      connid);
    /* fallthru */

 fail:
    if (set != NULL)
        mrp_resource_set_destroy(set);

    return NULL;
}


static int resctl_update(gamctl_t *gam, uint32_t rsetid,
                         uint16_t source, uint16_t sink, uint16_t conn)
{
    static int srcidx = -1, sinkidx = -1, connidx = -1;

    mrp_resource_set_t *rset  = mrp_resource_set_find_by_id(rsetid);
    mrp_attr_t         *attrs = NULL, *a;
    int                 i, status;

    MRP_UNUSED(gam);

    if (rset == NULL) {
        mrp_log_error("Failed to update resource set, can't find set 0x%x.",
                      rsetid);
        return FALSE;
    }

    attrs = mrp_resource_set_read_all_attributes(rset, "audio_playback", 0,NULL);

    if (attrs == NULL) {
        mrp_log_error("Failed to read resource set attribute list.");
        return FALSE;
    }

    if (srcidx >= 0 && sinkidx >= 0 && connidx >= 0) {
        attrs[srcidx].value.integer  = source;
        attrs[sinkidx].value.integer = sink;
        attrs[connidx].value.integer = conn;
    }
    else {
        for (a = attrs, i = 0; a->name != NULL; a++, i++) {
            if (a->type != mqi_integer)
                continue;

            if (!strcmp(a->name, "source_id")) {
                a->value.integer = source;
                srcidx = i;
            }
            else if (!strcmp(a->name, "sink_id")) {
                a->value.integer = sink;
                sinkidx = i;
            }
            else if (!strcmp(a->name, "connid")) {
                a->value.integer = conn;
                connidx = i;
            }
        }
    }

    status = mrp_resource_set_write_attributes(rset, "audio_playback", attrs);

    if (status < 0)
        mrp_log_error("Failed to update resource set attributes.");
    else
        mrp_log_info("Resource set attributes updated.");

    return status;
}


static void resctl_acquire(gamctl_t *gam, mrp_resource_set_t *set)
{
    mrp_log_info("Acquiring Genivi Audio Manager resource set.");
    mrp_resource_set_acquire(set, gam->seq++);
}


static int resctl_destroy(gamctl_t *gam, uint16_t connid)
{
    mrp_resource_set_t *rset;

    rset = mrp_htbl_remove(gam->rstbl, (void *)(ptrdiff_t)connid, FALSE);

    if (rset != NULL) {
        mrp_resource_set_destroy(rset);
        return TRUE;
    }
    else
        return FALSE;
}


static void resctl_recalc(gamctl_t *gam, int zoneid)
{
    uint32_t z;

    MRP_UNUSED(gam);

    mrp_log_info("Recalculating resource set allocations.");

    if (zoneid >= 0)
        mrp_resource_owner_recalc(zoneid);
    else
        for (z = 0; z < mrp_zone_count(); z++)
            mrp_resource_owner_recalc(z);
}


static void recalc_cb(mrp_deferred_t *d, void *user_data)
{
    gamctl_t *gam = (gamctl_t *)user_data;

    MRP_UNUSED(d);

    mrp_disable_deferred(gam->recalc);

    resctl_recalc(gam, -1);
}


static void resctl_schedule_recalc(gamctl_t *gam)
{
    mrp_log_info("Scheduling resource recalculation.");

    if (gam->recalc == NULL)
        gam->recalc = mrp_add_deferred(gam->self->ctx->ml, recalc_cb, gam);
    else
        mrp_enable_deferred(gam->recalc);
}


static void resctl_exit(gamctl_t *gam)
{
    if (gam != NULL) {
        if (gam->rsc != NULL) {
            mrp_log_info("Destroying Genivi Audio Manager resource client.");
            mrp_resource_client_destroy(gam->rsc);
            gam->rsc = NULL;
        }

        mrp_htbl_destroy(gam->rstbl, FALSE);
        gam->rstbl = NULL;
    }
}


static int route_init(gamctl_t *gam)
{
    route_t *r;
    path_t  *p;
    size_t   nroute, i;

    nroute      = MRP_ARRAY_SIZE(paths);
    gam->routes = mrp_allocz_array(route_t, nroute + 1);

    if (gam->routes == NULL)
        return FALSE;

    r = gam->routes;
    p = paths;
    for (i = 0; i < nroute; i++) {
        r->nhop = p->nhop;
        r->hops = mrp_allocz_array(uint16_t, r->nhop + 1);

        if (r->hops == NULL && r->nhop != 0)
            return FALSE;

        r->source = p->hops[0];
        r->sink   = p->hops[p->nhop - 1];

        mrp_log_info("Added a routing table entry for '%s' -> '%s'.",
                     r->source, r->sink);

        r++;
        p++;
    }

    gam->sourcef = 0;
    gam->sinkf   = 0;

    return TRUE;
}


static void route_exit(gamctl_t *gam)
{
    route_t *r;

    if (gam->routes == NULL)
        return;

    for (r = gam->routes; r->source != NULL; r++) {
        mrp_free(r->hops);
        r->hops = NULL;
        r->nhop = 0;
    }
}


static char *route_dump(gamctl_t *gam, char *buf, size_t size,
                        route_t *r, int verbose)
{
    path_t     *p;
    const char *t;
    char       *b;
    int         n, i;
    size_t      h, l;

    i = r - gam->routes;
    p = paths + i;

    t = "";
    b = buf;
    l = size;

    for (h = 0; h < r->nhop; h++) {
        if (verbose)
            n = snprintf(b, l, "%s%u(%s)", t, r->hops[h], p->hops[h]);
        else
            n = snprintf(b, l, "%s%u", t, r->hops[h]);

        if (n >= (int)l)
            return "dump_route: insufficient buffer";

        b += n;
        l -= n;
        t  = " -> ";
    }

    return buf;
}


static int route_incomplete(route_t *r)
{
    size_t h;

    for (h = 0; h < r->nhop; h++)
        if (!r->hops[h])
            return TRUE;

    return FALSE;
}


static route_t *route_connection(gamctl_t *gam, uint16_t source, uint16_t sink)
{
    uint32_t  sourcef = gam->sourcef;
    uint32_t  sinkf   = gam->sinkf;
    char      route[1024];
    route_t  *r;

    if (!resolve_nodes("source", sources, SOURCETBL, &gam->sourcef) ||
        !resolve_nodes("sink"  , sinks  , SINKTBL  , &gam->sinkf))
        return NULL;

    if (sourcef != gam->sourcef || sinkf != gam->sinkf)
        if (!resolve_routes(gam))
            return NULL;

    for (r = gam->routes; r->source; r++) {
        if (r->hops[0] == source && r->hops[r->nhop - 1] == sink) {
            if (!route_incomplete(r)) {
                mrp_log_info("Chosen route for connection: %s",
                             route_dump(gam, route, sizeof(route), r, TRUE));
                return r;
            }
            else {
                mrp_log_error("Route %u -> %u is unresolved/incomplete.",
                              source, sink);
                return NULL;
            }
        }
    }

    return NULL;
}


static int domctl_init(gamctl_t *gam)
{
    mrp_context_t           *ctx           = gam->self->ctx;
    mrp_domain_method_def_t  gam_methods[] = {
        { "request_route"    , 32, gamctl_route_cb     , gam },
        { "notify_disconnect",  8, gamctl_disconnect_cb, gam },
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
    gamctl_t           *gam = (gamctl_t *)user_data;
    uint16_t            source, sink, conn, *path;
    uint32_t            rsetid;
    const char         *error;
    size_t              i;
    mrp_resource_set_t *rset;
    route_t            *r;

    if (narg < 4) {
        error = "too few route request arguments (need route, conn, "
            "rset and paths)";
        goto error;
    }

    if (args[0].type != MRP_DOMCTL_ARRAY(UINT16)) {
        error = "invalid route (arg #0), array of uint16_t expected";
        goto error;
    }

    if (args[0].size != 2) {
        error = "invalid route (arg #0), 2 endpoints expected";
        goto error;
    }

    if (args[1].type != MRP_DOMCTL_UINT16) {
        error = "invalid connection id (arg #1), uint16_t expected";
        goto error;
    }

    if (args[2].type != MRP_DOMCTL_UINT32) {
        error = "invalid resource set id (arg #2), uint32_t expected";
        goto error;
    }

    source = ((uint16_t *)args[0].arr)[0];
    sink   = ((uint16_t *)args[0].arr)[1];
    conn   = args[1].u16;
    rsetid = args[2].u32;

    mrp_log_info("Got routing request for connection #%u:%u -> %u "
                 "(rset 0x%x) with %d possible routes.", conn, source, sink,
                 rsetid, narg - 3);

    r = route_connection(gam, source, sink);

    if (r == NULL || r->nhop == 0) {
        error = "no route";
        goto error;
    }

    if (!rsetid) {
        if ((rset = resctl_create(gam, source, sink, conn, 0)) == NULL) {
            error = "failed to create resouce set";
            goto error;
        }

        resctl_acquire(gam, rset);
    }
    else {
        resctl_update(gam, rsetid, source, sink, conn);
        resctl_schedule_recalc(gam);
    }

    path = mrp_allocz(r->nhop * sizeof(path[0]));

    if (path == NULL) {
        error = NULL;
        goto error;
    }

    for (i = 0; i < r->nhop; i++)
        path[i] = r->hops[i];

    *nout = 1;
    outs[0].type = MRP_DOMCTL_ARRAY(UINT16);
    outs[0].arr  = path;
    outs[0].size = r->nhop;

    return 0;

 error:
    *nout = 1;
    outs[0].type = MRP_DOMCTL_STRING;
    outs[0].str  = mrp_strdup(error);

    return -1;
}


static int gamctl_disconnect_cb(int narg, mrp_domctl_arg_t *args,
                                uint32_t *nout, mrp_domctl_arg_t *outs,
                                void *user_data)
{
    gamctl_t   *gam = (gamctl_t *)user_data;
    const char *error;
    uint16_t    conn;

    if (narg != 1) {
        error = "too few disconnect notification arguments (need connid)";
        goto error;
    }

    if (args[0].type != MRP_DOMCTL_UINT16) {
        error = "invalid disconnect connid (arg #0), uint16_t expected";
        goto error;
    }

    conn = args[0].u16;

    mrp_log_info("Got disconnect request for connection #%u.", conn);

    resctl_destroy(gam, conn);
    return TRUE;

 error:
    *nout = 1;
    outs[0].type = MRP_DOMCTL_STRING;
    outs[0].str  = mrp_strdup(error);

    return FALSE;
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

    if (!route_init(gam))
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
    route_exit(gam);
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
