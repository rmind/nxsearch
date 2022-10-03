/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _TOK_FILTERS_H_
#define _TOK_FILTERS_H_

#include "strbuf.h"

struct filter_pipeline;
typedef struct filter_pipeline filter_pipeline_t;

typedef enum {
	FILT_ERROR	= -1,
	FILT_MUTATION	= 0,
	FILT_DROP	= 1,
} filter_action_t;

typedef struct filter_ops {
	void *		(*sysinit)(nxs_t *);
	void		(*sysfini)(void *);

	void *		(*create)(nxs_params_t *, void *);
	void		(*destroy)(void *);
	filter_action_t	(*filter)(void *, strbuf_t *);
} filter_ops_t;

/*
 * Filter registration and builtin filters.
 */

int		filters_sysinit(nxs_t *);
void		filters_sysfini(nxs_t *);
int		filters_builtin_sysinit(nxs_t *);

/*
 * Filter pipeline.
 */

filter_pipeline_t *filter_pipeline_create(nxs_t *, nxs_params_t *);
void		filter_pipeline_destroy(filter_pipeline_t *);
filter_action_t	filter_pipeline_run(filter_pipeline_t *, strbuf_t *);

#endif
