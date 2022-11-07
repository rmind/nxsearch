/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Builtin filters.
 *
 * Typical tokenization pipeline:
 *
 *	tokenizer => normalizer -> stopword filter -> stemmer => terms
 */

#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>

#include <libstemmer.h>

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "filters.h"
#include "strbuf.h"
#include "tokenizer.h"
#include "rhashmap.h"
#include "utf8.h"
#include "utils.h"

#define	DUMMY_PTR	((void *)(uintptr_t)0x1)

/*
 * Basic token normalizer.
 */

static void *
normalizer_create(nxs_params_t *params, void *arg __unused)
{
	const char *lang = nxs_params_get_str(params, "lang");
	return utf8_ctx_create(lang);
}

static void
normalizer_destroy(void *arg)
{
	utf8_ctx_t *ctx = arg;
	utf8_ctx_destroy(ctx);
}

static filter_action_t
normalizer_filter(void *arg, strbuf_t *buf)
{
	utf8_ctx_t *ctx = arg;

	/*
	 * Lowercase and Unicode NFKC normalization.
	 */
	if (utf8_normalize(ctx, buf) == -1) {
		app_dbgx("normalization of [%s] failed", buf->value);
		return FILT_ERROR;
	}

	/*
	 * Substitute diacritics.
	 */
	if (utf8_subs_diacritics(ctx, buf) == -1) {
		app_dbgx("diacritics substitution on [%s] failed", buf->value);
		return FILT_ERROR;
	}

	return FILT_MUTATION;
}

static const filter_ops_t normalizer_ops = {
	.create		= normalizer_create,
	.destroy	= normalizer_destroy,
	.filter		= normalizer_filter,
};

/*
 * Stopwords.
 */

static void		stopwords_sysfini(void *);
static const char *	stopword_langs[] = { "en" };  // TODO/XXX

static int
stopwords_load(nxs_t *nxs, rhashmap_t *swdicts, const char *lang)
{
	rhashmap_t *lmap;
	char *dbpath, *line = NULL;
	size_t lcap = 0;
	ssize_t len;
	FILE *fp;

	if (asprintf(&dbpath, "%s/filters/stopwords/%s",
	    nxs->basedir, lang) == -1) {
		return -1;
	}
	fp = fopen(dbpath, "r");
	free(dbpath);
	if (fp == NULL) {
		/* No stop words. */
		return 0;
	}

	if ((lmap = rhashmap_create(0, RHM_NONCRYPTO)) == NULL) {
		return -1;
	}
	while ((len = getline(&line, &lcap, fp)) > 0) {
		if (len <= 1) {
			continue;
		}
		line[--len] = '\0';
		rhashmap_put(lmap, line, len, DUMMY_PTR);
	}
	free(line);
	fclose(fp);

	rhashmap_put(swdicts, lang, strlen(lang), lmap);
	return 0;
}

static void *
stopwords_sysinit(nxs_t *nxs, void *arg __unused)
{
	rhashmap_t *swdicts;

	if ((swdicts = rhashmap_create(0, RHM_NONCRYPTO)) == NULL) {
		return NULL;
	}
	for (unsigned i = 0; i < __arraycount(stopword_langs); i++) {
		const char *lang = stopword_langs[i];

		if (stopwords_load(nxs, swdicts, lang) == -1) {
			stopwords_sysfini(swdicts);
			return NULL;
		}
	}
	return swdicts;
}

static void
stopwords_sysfini(void *ctx)
{
	rhashmap_t *swdicts;

	if ((swdicts = ctx) == NULL) {
		return;
	}
	for (unsigned i = 0; i < __arraycount(stopword_langs); i++) {
		const char *lang = stopword_langs[i];
		rhashmap_t *lmap;

		lmap = rhashmap_get(swdicts, lang, strlen(lang));
		if (lmap) {
			rhashmap_destroy(lmap);
		}
	}
	rhashmap_destroy(swdicts);
}

static void *
stopwords_create(nxs_params_t *params, void *arg)
{
	rhashmap_t *swdicts = arg;
	const char *lang = nxs_params_get_str(params, "lang");
	rhashmap_t *lang_map;

	if ((lang_map = rhashmap_get(swdicts, lang, strlen(lang))) != NULL) {
		return lang_map;
	}

	app_dbgx("no stopwords for '%s' language", lang);
	return DUMMY_PTR;  // not an error
}

static filter_action_t
stopwords_filter(void *arg, strbuf_t *buf)
{
	rhashmap_t *lang_map = arg;

	if (lang_map != DUMMY_PTR &&
	    rhashmap_get(lang_map, buf->value, buf->length)) {
		return FILT_DROP;
	}
	return FILT_MUTATION;  // pass-through
}

static const filter_ops_t stopwords_ops = {
	.sysinit	= stopwords_sysinit,
	.sysfini	= stopwords_sysfini,
	.create		= stopwords_create,
	.destroy	= NULL,  // NOP
	.filter		= stopwords_filter,
};

/*
 * Stemmer
 */

static void *
stemmer_create(nxs_params_t *params, void *arg __unused)
{
	const char *lang = nxs_params_get_str(params, "lang");
	return sb_stemmer_new(lang, NULL /* UTF-8 */);
}

static void
stemmer_destroy(void *arg)
{
	struct sb_stemmer *sbs = arg;
	sb_stemmer_delete(sbs);
}

static filter_action_t
stemmer_filter(void *arg, strbuf_t *buf)
{
	struct sb_stemmer *sbs = arg;
	const char *stemmed;
	size_t len;

	stemmed = (const char *)sb_stemmer_stem(sbs,
	    (const unsigned char *)buf->value, buf->length);
	len = sb_stemmer_length(sbs);

	/*
	 * NOTE: The return value of sb_stemmer_stem() is global and
	 * must copied (unfortunately, the API is not re-entrant).
	 */
	if (strbuf_acquire(buf, stemmed, len) == -1) {
		return FILT_ERROR;
	}
	return FILT_MUTATION;
}

static const filter_ops_t stemmer_ops = {
	.create		= stemmer_create,
	.destroy	= stemmer_destroy,
	.filter		= stemmer_filter,
};

/*
 * Register the builtin filters.
 */

int
filters_builtin_sysinit(nxs_t *nxs)
{
	nxs_filter_register(nxs, "normalizer", &normalizer_ops, NULL);
	nxs_filter_register(nxs, "stopwords", &stopwords_ops, NULL);
	nxs_filter_register(nxs, "stemmer", &stemmer_ops, NULL);
	return 0;
}
