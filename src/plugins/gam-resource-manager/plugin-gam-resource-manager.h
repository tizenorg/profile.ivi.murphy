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

#ifndef __MURPHY_GAM_RESOURCE_MANAGER_H__
#define __MURPHY_GAM_RESOURCE_MANAGER_H__

#include <murphy/resource/data-types.h>

#define MRP_RESMGR_PLAYBACK_RESOURCE        "audio_playback"
#define MRP_RESMGR_RECORDING_RESOURCE       "audio_recording"

#define MRP_RESMGR_SOURCE_STATE_TABLE       "audio_manager_sources"
#define MRP_RESMGR_SINK_STATE_TABLE         "audio_manager_sinks"

#define MRP_RESMGR_DEFAULT_CONFDIR          "/etc/murphy/gam_config"
#define MRP_RESMGR_DEFAULT_PREFIX           "gam"
#define MRP_RESMGR_DEFAULT_NAMES            MRP_RESMGR_DEFAULT_PREFIX
#define MRP_RESMGR_RESOURCE_MAX             256
#define MRP_RESMGR_SOURCE_MAX               64
#define MRP_RESMGR_SINK_MAX                 64
#define MRP_RESMGR_USECASE_SIZE_MAX         128

#define MRP_RESMGR_RESOURCE_BUCKETS         (MRP_RESMGR_RESOURCE_MAX / 4)
#define MRP_RESMGR_SOURCE_BUCKETS           (MRP_RESMGR_SOURCE_MAX / 4)
#define MRP_RESMGR_SINK_BUCKETS             (MRP_RESMGR_SINK_MAX / 4)
#define MRP_RESMGR_USECASE_SIZE_BUCKETS     (MRP_RESMGR_USECASE_SIZE_MAX / 4)

#define MRP_RESMGR_RESOURCE_TYPE_PLAYBACK   0
#define MRP_RESMGR_RESOURCE_TYPE_RECORDING  1
#define MRP_RESMGR_RESOURCE_TYPE_MAX        2

typedef struct mrp_resmgr_s                 mrp_resmgr_t;
typedef struct mrp_resmgr_config_s          mrp_resmgr_config_t;
typedef struct mrp_resmgr_backend_s         mrp_resmgr_backend_t;
typedef struct mrp_resmgr_resource_s        mrp_resmgr_resource_t;
typedef struct mrp_resmgr_sources_s         mrp_resmgr_sources_t;
typedef struct mrp_resmgr_source_s          mrp_resmgr_source_t;
typedef struct mrp_resmgr_sinks_s           mrp_resmgr_sinks_t;
typedef struct mrp_resmgr_sink_s            mrp_resmgr_sink_t;
typedef struct mrp_resmgr_usecase_s         mrp_resmgr_usecase_t;

typedef bool (*mrp_resmgr_dependency_cb_t)(mrp_resmgr_t *);

struct mrp_resmgr_config_s {
    const char *confdir;
    const char *prefix;
    const char *confnams;
    int max_active;
};

void mrp_resmgr_register_dependency(mrp_resmgr_t *resmgr,
                                    const char *db_table_name,
                                    mrp_resmgr_dependency_cb_t callback);

mrp_resmgr_config_t *mrp_resmgr_get_config(mrp_resmgr_t *resmgr);
mrp_resmgr_backend_t *mrp_resmgr_get_backend(mrp_resmgr_t *resmgr);
mrp_resmgr_sources_t *mrp_resmgr_get_sources(mrp_resmgr_t *resmgr);
mrp_resmgr_sinks_t *mrp_resmgr_get_sinks(mrp_resmgr_t *resmgr);
mrp_resmgr_usecase_t *mrp_resmgr_get_usecase(mrp_resmgr_t *resmgr);

#endif  /* __MURPHY_GAM_RESOURCE_MANAGER_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
