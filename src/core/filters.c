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

typedef struct {
	void *			context;
	const filter_ops_t *	ops;
} filter_t;

struct filter_entry {
	const char *		name;
	const filter_ops_t *	ops;
};

struct filter_pipeline {
	char			lang[3];
	unsigned		count;
	filter_t		filters[];
};

int
filters_sysinit(nxs_t *nxs)
{
	nxs->filters = calloc(FILTER_MAX_ENTRIES, sizeof(filter_entry_t));
	if (nxs->filters == NULL) {
		return -1;
	}
	if (filters_builtin_sysinit(nxs) == -1) {
		free(nxs->filters);
		return -1;
	}
	return 0;
}

void
filters_sysfini(nxs_t *nxs)
{
	filters_builtin_sysfini(nxs);
	free(nxs->filters);
}

static const filter_ops_t *
filter_lookup(nxs_t *nxs, const char *name)
{
	const filter_ops_t *ops = NULL;

	for (unsigned i = 0; i < nxs->filters_count; i++) {
		const filter_entry_t *filtent = &nxs->filters[i];

		if (strcmp(filtent->name, name) == 0) {
			ops = filtent->ops;
			break;
		}
	}
	return ops;
}

__dso_public int
nxs_filter_register(nxs_t *nxs, const char *name, const filter_ops_t *ops)
{
	unsigned count = nxs->filters_count;

	ASSERT(name && ops);

	if (count == FILTER_MAX_ENTRIES) {
		return -1;
	}
	if (filter_lookup(nxs, name)) {
		errno = EEXIST;
		return -1;
	}
	nxs->filters[count].name = name;
	nxs->filters[count].ops = ops;
	nxs->filters_count++;
	return 0;
}

/*
 * filter_pipeline_create: construct a new pipeline of filters.
 *
 * => Language must be a two letter ISO 639-1 code.
 */
filter_pipeline_t *
filter_pipeline_create(nxs_t *nxs, const char *lang,
    const char *filters[], size_t count)
{
	filter_pipeline_t *fp;
	size_t len;

	// TODO: verify the language

	len = offsetof(filter_pipeline_t, filters[count]);
	if ((fp = calloc(1, len)) == NULL) {
		return NULL;
	}
	fp->count = count;

	/* Save the language code. */
	strncpy(fp->lang, lang, sizeof(fp->lang) - 1);
	fp->lang[sizeof(fp->lang) - 1] = '\0';

	for (unsigned i = 0; i < count; i++) {
		const char *name = filters[i];
		filter_t *filt = &fp->filters[i];

		if ((filt->ops = filter_lookup(nxs, name)) == NULL) {
			goto err;
		}
		if (filt->ops->create == NULL) {
			continue;
		}
		filt->context = filt->ops->create(nxs, lang);
		if (filt->context == NULL) {
			goto err;
		}
	}
	return fp;
err:
	filter_pipeline_destroy(fp);
	return NULL;
}

void
filter_pipeline_destroy(filter_pipeline_t *fp)
{
	for (unsigned i = 0; i < fp->count; i++) {
		filter_t *filt = &fp->filters[i];

		if (filt->ops && filt->ops->destroy) {
			filt->ops->destroy(filt->context);
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

		action = ops->filter(filt->context, buf);
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
