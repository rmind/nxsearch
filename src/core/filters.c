/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Filters.
 *
 * They are used to transform tokens such that they are more suitable
 * for searching.  This module implements an interface to register filters
 * and create pipelines which be invoked by the tokenizer.
 */

#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#define	__NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "filters.h"
#include "utils.h"

/*
 * Filter entry with any long-term context.
 */
struct filter_entry {
	const char *		name;
	const filter_ops_t *	ops;
	void *			context;
	TAILQ_ENTRY(filter_entry) entry;
};

/*
 * Filter with its per-pipeline argument.
 */
typedef struct {
	void *			arg;
	const filter_ops_t *	ops;
} filter_t;

struct filter_pipeline {
	unsigned		count;
	filter_t		filters[];
};

int
filters_sysinit(nxs_t *nxs)
{
	TAILQ_INIT(&nxs->filter_list);

	if (filters_builtin_sysinit(nxs) == -1) {
		return -1;
	}
	return 0;
}

void
filters_sysfini(nxs_t *nxs)
{
	filter_entry_t *filtent;

	while ((filtent = TAILQ_FIRST(&nxs->filter_list)) != NULL) {
		const filter_ops_t *ops = filtent->ops;

		if (ops && ops->sysfini) {
			ops->sysfini(filtent->context);
		}
		TAILQ_REMOVE(&nxs->filter_list, filtent, entry);
		free(filtent);
	}
}

static filter_entry_t *
filter_lookup(nxs_t *nxs, const char *name)
{
	filter_entry_t *filtent;

	ASSERT(name != NULL);

	TAILQ_FOREACH(filtent, &nxs->filter_list, entry) {
		if (strcmp(filtent->name, name) == 0) {
			return filtent;
		}
	}
	return NULL;
}

__dso_public int
nxs_filter_register(nxs_t *nxs, const char *name, const filter_ops_t *ops)
{
	filter_entry_t *filtent;

	if (filter_lookup(nxs, name)) {
		errno = EEXIST;
		return -1;
	}
	if ((filtent = calloc(1, sizeof(filter_entry_t))) == NULL) {
		return -1;
	}
	if (ops->sysinit && (filtent->context = ops->sysinit(nxs)) == NULL) {
		free(filtent);
		return -1;
	}
	filtent->name = name;
	filtent->ops = ops;

	TAILQ_INSERT_TAIL(&nxs->filter_list, filtent, entry);
	nxs->filters_count++;
	return 0;
}

/*
 * filter_pipeline_create: construct a new pipeline of filters.
 */
filter_pipeline_t *
filter_pipeline_create(nxs_t *nxs, nxs_params_t *params)
{
	const char **filters;
	filter_pipeline_t *fp;
	size_t count = 0, len;

	/*
	 * Extract the filter list from the parameters.
	 *
	 * Note: support no filters in which case the count will be zero.
	 * We will allocate all the structures to keep the code simple,
	 * but is will effectively be a NOP.
	 */
	filters = nxs_params_get_strlist(params, "filters", &count);
	ASSERT(filters || count == 0);

	len = offsetof(filter_pipeline_t, filters[count]);
	if ((fp = calloc(1, len)) == NULL) {
		free(filters);
		return NULL;
	}
	fp->count = count;

	for (unsigned i = 0; i < count; i++) {
		const char *name = filters[i];
		filter_t *filt = &fp->filters[i];
		filter_entry_t *filtent;

		if ((filtent = filter_lookup(nxs, name)) == NULL) {
			goto err;
		}

		filt->ops = filtent->ops;
		if (filt->ops->create == NULL) {
			continue;
		}

		filt->arg = filt->ops->create(params, filtent->context);
		if (filt->arg == NULL) {
			goto err;
		}
	}
	free(filters);
	return fp;
err:
	filter_pipeline_destroy(fp);
	free(filters);
	return NULL;
}

void
filter_pipeline_destroy(filter_pipeline_t *fp)
{
	for (unsigned i = 0; i < fp->count; i++) {
		filter_t *filt = &fp->filters[i];

		if (filt->ops && filt->ops->destroy) {
			filt->ops->destroy(filt->arg);
		}
	}
	free(fp);
}

/*
 * filter_pipeline_run: apply the filters.
 *
 * Mutates the given string buffer.  Filters may return a new string
 * buffer (output), if the former is too small.
 */
filter_action_t
filter_pipeline_run(filter_pipeline_t *fp, strbuf_t *buf)
{
	for (unsigned i = 0; i < fp->count; i++) {
		filter_t *filt = &fp->filters[i];
		const filter_ops_t *ops = filt->ops;
		filter_action_t action;

		action = ops->filter(filt->arg, buf);
		if (__predict_false(buf->length == 0)) {
			return FILT_DROP;
		}
		ASSERT(buf->value[buf->length] == '\0');
		app_dbgx("[%s] filter %u action %d", buf->value, i, action);

		if (__predict_false(action != FILT_MUTATION)) {
			return action;
		}
	}
	return FILT_MUTATION;
}
