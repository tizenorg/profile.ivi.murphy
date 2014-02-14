/*
 * Copyright (c) 2012, 2013, Intel Corporation
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

#ifndef __MURPHY_SYSCTL_ESTIMATOR_H__
#define __MURPHY_SYSCTL_ESTIMATOR_H__

#include <math.h>

#include <murphy/common/mm.h>


/*
 * exponentially weighted moving average
 */

typedef struct  {
    double a;                            /* smoothing factor */
    double sample;                       /* last sample fed in */
    double S;                            /* current estimate */
} ewma_t;


/** Initialize the given EWMA estimator. */
static inline void ewma_init(ewma_t *e, double alpha, double sample)
{
    e->S = e->sample = sample;
    e->a = alpha;
}


/** Allocate and initialize a new EWMA estimator. */
static inline ewma_t *ewma_create(double alpha, double sample)
{
    ewma_t *e;

    if ((e = mrp_allocz(sizeof(*e))) != NULL)
        ewma_init(e, alpha, sample);

    return e;
}


/** Free an EWMA estimator created by ewma_create. */
static inline void ewma_free(ewma_t *e)
{
    mrp_free(e);
}


/** Reset the given EWMA estimator. */
#define ewma_reset(e, alpha, sample) ewma_init(e, alpha, sample)


/** Push a new sample into the EWMA estimation buffer. */
static inline double ewma_add(ewma_t *e, double sample)
{
    e->sample = sample;
    e->S = e->a * sample + (1 - e->a) * e->S;

    return e->S;
}


/** Read the current estimate from the EWMA estimation buffer. */
static inline double ewma_value(ewma_t *e)
{
    return e->S;
}


/** Read the last sample fed in. */
static inline double ewma_sample(ewma_t *e)
{
    return e->sample;
}


#endif /* __MURPHY_SYSCTL_ESTIMATOR_H__ */
