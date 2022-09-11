/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _TOK_FILTERS_H_
#define _TOK_FILTERS_H_

#include "strbuf.h"

#define	FILTER_MAX_ENTRIES	64

struct nxs;

typedef struct filter_entry filter_entry_t;
typedef struct filter_pipeline filter_pipeline_t;

typedef enum {
	FILT_ERROR	= -1,
	FILT_MUTATION	= 0,
	FILT_DROP	= 1,
} filter_action_t;

typedef struct filter_ops {
	void *		(*create)(const char *);
	void		(*destroy)(void *);
	filter_action_t	(*filter)(void *, strbuf_t *);
} filter_ops_t;

/*
 * Filter registration and builtin filters.
 */

int		filters_sysinit(struct nxs *);
void		filters_sysfini(struct nxs *);
int		filters_builtin_sysinit(struct nxs *);

/*
 * Filter pipeline.
 */

filter_pipeline_t *filter_pipeline_create(struct nxs *,
		    const char *, const char *[], size_t);
void		filter_pipeline_destroy(filter_pipeline_t *);
filter_action_t	filter_pipeline_run(filter_pipeline_t *, strbuf_t *);

#endif
