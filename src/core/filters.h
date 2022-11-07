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
	FILT_DISCARD	= 1,
} filter_action_t;

typedef struct filter_ops {
	/*
	 * sysinit/sysfini handlers are called on nxsearch instantiation
	 * and may be used to setup long-term context / resources, e.g.
	 * load the dictionaries.
	 */
	void *		(*sysinit)(nxs_t *, void *);
	void		(*sysfini)(void *);

	/*
	 * create/destroy handlers are called upon the filter pipeline
	 * creation/destruction, when opening a particular index, and
	 * may be used to obtain index parameters (such as the language)
	 * and setup index-specific resources.
	 */
	void *		(*create)(nxs_params_t *, void *);
	void		(*destroy)(void *);

	/*
	 * The main filter handler: takes the string buffer and either
	 * mutates it, discards it or indicates and error.
	 */
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
