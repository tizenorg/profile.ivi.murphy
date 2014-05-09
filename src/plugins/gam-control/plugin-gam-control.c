#include <stdlib.h>
#include <syslog.h>

#include <murphy/common.h>
#include <murphy/core.h>
#include <murphy/core/domain.h>


static int request_route_cb(int narg, mrp_domctl_arg_t *args,
                            uint32_t *nout, mrp_domctl_arg_t *outs,
                            void *user_data)
{
    mrp_plugin_t *plugin = (mrp_plugin_t *)user_data;
    const char   *error;
    uint16_t     *path, *route;
    size_t        len, i;

    MRP_UNUSED(plugin);

    if (narg < 2) {
        error = "invalid number of arguments (expecting >= 2)";
        goto error;
    }

    if (args[0].type != MRP_DOMCTL_ARRAY(UINT16) ||
        args[1].type != MRP_DOMCTL_ARRAY(UINT16)) {
        error = "invalid argument types (expecting arrays of uint16_t)";
        goto error;
    }

    mrp_log_info("Got routing request for %u -> %u with %d possible routes.",
                 ((uint16_t *)args[0].arr)[0], ((uint16_t *)args[0].arr)[1],
                 narg - 1);

    route = args[1].arr;
    len   = args[1].size;

    if (len == 0) {
        error = "invalid route (empty)";
        goto error;
    }

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
    mrp_domain_method_def_t methods[] = {
        { "request_route", 32, request_route_cb, plugin },
    };
    size_t nmethod = MRP_ARRAY_SIZE(methods);

    if (!mrp_register_domain_methods(plugin->ctx, methods, nmethod)) {
        mrp_log_error("Failed to register gam-control domain methods.");
        return FALSE;
    }
    else
        return TRUE;
}


static void gamctl_exit(mrp_plugin_t *plugin)
{
    MRP_UNUSED(plugin);

    return;
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
