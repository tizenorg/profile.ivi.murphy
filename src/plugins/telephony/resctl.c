#include <stdbool.h>

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>
#include <murphy/common/log.h>
#include <murphy/resource/client-api.h>

#include "resctl.h"

struct resctl_s {
    mrp_resource_client_t *client;
    mrp_resource_set_t    *set;
    uint32_t               seqno;
    int                    requested;
    int                    granted;
};

static void event_cb(uint32_t reqid, mrp_resource_set_t *rset, void *user_data);


typedef struct {
    char     *zone;
    char     *cls;
    uint32_t  prio;
    char     *play;
    int       pmnd;
    int       pshr;
    char     *rec;
    int       rmnd;
    int       rshr;
    char     *role;
} config_t;


static config_t cfg;

#define EXCLUSIVE "exclusive"
#define SHARED    "shared"
#define MANDATORY "mandatory"
#define OPTIONAL  "optional"
#define EXCLEN    (sizeof(EXCLUSIVE) - 1)
#define SHRLEN    (sizeof(SHARED)    - 1)
#define MNDLEN    (sizeof(MANDATORY) - 1)
#define OPTLEN    (sizeof(OPTIONAL)  - 1)

#define PLAYBACK  "playback"
#define RECORDING "recording"

static int parse_resource(const char *key, const char *value)
{
    const char  *p, *e;
    char       **resp;
    int         *mndp, *shrp, l;

    if (!strcmp(key, PLAYBACK)) {
        resp = &cfg.play;
        mndp = &cfg.pmnd;
        shrp = &cfg.pshr;
    }
    else if (!strcmp(key, RECORDING)) {
        resp = &cfg.rec;
        mndp = &cfg.rmnd;
        shrp = &cfg.rshr;
    }
    else
        return FALSE;

    mrp_free(*resp);
    *resp = NULL;
    *mndp = 1;
    *shrp = 0;

    p = value;
    while (p) {
        while (*p && (*p == ' ' || *p == '\t' || *p == ','))
            p++;

        e = strchr(p, ',');

        if (e != NULL)
            l = e - p;
        else
            l = strlen(p);

        if      (l == SHRLEN && !strncmp(p, SHARED   , l)) *shrp = 1;
        else if (l == EXCLEN && !strncmp(p, EXCLUSIVE, l)) *shrp = 0;
        else if (l == OPTLEN && !strncmp(p, OPTIONAL , l)) *mndp = 0;
        else if (l == MNDLEN && !strncmp(p, MANDATORY, l)) *mndp = 1;
        else {
            if (*resp != NULL || (*resp = mrp_datadup((void *)p, l+1)) == NULL)
                return FALSE;

            (*resp)[l] = '\0';
        }

        p = e && *e ? e + 1 : NULL;
    }

    if (*resp != NULL)
        return TRUE;
    else
        return FALSE;
}


int resctl_config(const char *zone, const char *cls, uint32_t priority,
                  const char *playback, const char *recording, const char *role)
{
    mrp_free(cfg.zone);
    mrp_free(cfg.cls);
    mrp_free(cfg.play);
    mrp_free(cfg.rec);
    mrp_free(cfg.role);
    mrp_clear(&cfg);

    if (!(cfg.zone = mrp_strdup(zone)) ||
        !(cfg.cls  = mrp_strdup(cls)) ||
        !(cfg.role = mrp_strdup(role)))
        goto fail;

    if (!parse_resource(PLAYBACK , playback) ||
        !parse_resource(RECORDING, recording))
        goto fail;

    cfg.prio = priority;

    mrp_log_info("Telephony resource-set configuration:");
    mrp_log_info("    zone: %s, class: %s, priority: %u", cfg.zone, cfg.cls,
                 cfg.prio);
    mrp_log_info("    playback: %s %s %s%s%s",
                 cfg.pmnd ? "mandatory" : "optional",
                 cfg.pshr ? "shared" : "exclusive",
                 cfg.play,
                 cfg.role && *cfg.role ? " role:s:" : "",
                 cfg.role && *cfg.role ? cfg.role   : "");
    mrp_log_info("    recording: %s %s %s%s%s",
                 cfg.rmnd ? "mandatory" : "optional",
                 cfg.rshr ? "shared" : "exclusive",
                 cfg.rec,
                 cfg.role && *cfg.role ? " role:s:" : "",
                 cfg.role && *cfg.role ? cfg.role   : "");

    return TRUE;

 fail:
    mrp_free(cfg.zone);
    mrp_free(cfg.cls);
    mrp_free(cfg.play);
    mrp_free(cfg.rec);
    mrp_free(cfg.role);
    mrp_clear(&cfg);

    return FALSE;
}


resctl_t *resctl_init(void)
{
    const char            *zone   = cfg.zone;
    const char            *cls    = cfg.cls;
    uint32_t               prio   = cfg.prio;
    const char            *play   = cfg.play;
    bool                   pshr   = !!cfg.pshr;
    bool                   pmnd   = !!cfg.pmnd;
    const char            *rec    = cfg.rec;
    bool                   rshr   = !!cfg.rshr;
    bool                   rmnd   = !!cfg.rmnd;
    const char            *role   = cfg.role && *cfg.role ? cfg.role : NULL;
    bool                   ar     = false;
    bool                   nowait = false;
    resctl_t              *ctl;
    mrp_resource_client_t *client;
    mrp_resource_set_t    *set;
    mrp_attr_t             attrs[3], *attrp;

    ctl    = NULL;
    client = NULL;
    set    = NULL;

    ctl = mrp_allocz(sizeof(*ctl));

    if (ctl == NULL) {
        mrp_log_error("Failed to initialize telephony resource control.");
        goto fail;
    }

    client = mrp_resource_client_create("telephony", ctl);

    if (client == NULL) {
        mrp_log_error("Failed to create telephony resource client.");
        goto fail;
    }

    set = mrp_resource_set_create(client, ar, nowait, prio, event_cb, ctl);

    if (set == NULL) {
        mrp_log_error("Failed to create telephony resource set.");
        goto fail;
    }

    attrs[0].name = "policy";
    attrs[0].type = mqi_string;
    attrs[0].value.string = "relaxed";

    if (role != NULL) {
        attrs[1].type = mqi_string;
        attrs[1].name = "role";
        attrs[1].value.string = role;
        attrs[2].name = NULL;
    }
    else
        attrs[1].name = NULL;

    attrp = &attrs[0];

    if (mrp_resource_set_add_resource(set, play, pshr, attrp, pmnd) < 0 ||
        mrp_resource_set_add_resource(set, rec , rshr, attrp, rmnd) < 0) {
        mrp_log_error("Failed to initialize telephony resource set.");
        goto fail;
    }

    if (mrp_application_class_add_resource_set(cls, zone, set, 0) != 0) {
        mrp_log_error("Failed to assign telephony resource set with class.");
        goto fail;
    }

    ctl->client = client;
    ctl->set    = set;
    ctl->seqno  = 1;

    return ctl;

 fail:
    if (set != NULL)
        mrp_resource_set_destroy(set);

    if (client != NULL)
        mrp_resource_client_destroy(client);

    mrp_free(ctl);

    return NULL;
}


void resctl_exit(resctl_t *ctl)
{
    if (ctl != NULL) {
        if (ctl->set != NULL)
            mrp_resource_set_destroy(ctl->set);

        if (ctl->client != NULL)
            mrp_resource_client_destroy(ctl->client);

        mrp_free(ctl);
    }

    mrp_free(cfg.zone);
    mrp_free(cfg.cls);
    mrp_free(cfg.play);
    mrp_free(cfg.rec);
    mrp_free(cfg.role);
    mrp_clear(&cfg);
}


void resctl_acquire(resctl_t *ctl)
{
    if (ctl != NULL && !ctl->granted && !ctl->requested) {
        mrp_log_info("acquiring telephony resources");
        mrp_resource_set_acquire(ctl->set, ctl->seqno++);
        ctl->requested = TRUE;
    }
}


void resctl_release(resctl_t *ctl)
{
    if (ctl != NULL && (ctl->granted || ctl->requested)) {
        mrp_log_info("releasing telephony resources");
        mrp_resource_set_release(ctl->set, ctl->seqno++);
        ctl->requested = FALSE;
    }
}


static void event_cb(uint32_t reqid, mrp_resource_set_t *rset, void *user_data)
{
    resctl_t   *ctl = (resctl_t *)user_data;
    const char *state;

    if (mrp_get_resource_set_state(rset) == mrp_resource_acquire) {
        state = (reqid != 0 ? "acquired" : "got");
        ctl->granted = TRUE;
    }
    else {
        state = (reqid != 0 ? "released" : "lost");
        ctl->granted = FALSE;
    }

    mrp_log_info("telephony has %s audio resources.", state);
}
