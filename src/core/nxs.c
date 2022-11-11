/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * nxsearch: a full-text search engine.
 *
 * Overview
 *
 *	The main operations are: *adding* to the index (i.e. indexing
 *	a document), *searching* the index (running a particular query)
 *	and *deleting* the document from the index.
 *
 * Index a document
 *
 *	Basic document indexing involves the following steps:
 *
 *	- Tokenizing the given text and running the filters which may
 *	transform each token, typically using various linguistic methods,
 *	in order to accommodate the searching logic.
 *
 *	- Resolving the tokens to get the terms (i.e. the objects tracking
 *	the processed tokens already present in the index).  If the term
 *	is not in the index, then it gets added to the "staging" list which
 *	and then added to the term list (index) by idx_terms_add().
 *
 *	- Adding the document record with the set of term IDs (and their
 *	frequency in the document) to the document-term index ("dtmap").
 *
 * Indexes
 *
 *	There are two data files which form the index:
 *
 *	- Terms list (nxsterms.db) which contains the full list of all
 *	indexed terms.  The term ID is determined by the term order in
 *	the index starting from 1.
 *
 *	- Document-term mapping (nxsdtmap.db) contains the mappings of
 *	document IDs to the set of terms from the aforementioned list,
 *	referenced by their IDs.  The term IDs, associated with a document,
 *	are accompanied by a count which represents the term occurrences
 *	in the document.
 *
 *	Using the above two, an in-memory *reverse* index is built:
 *
 *		term_1 => [doc_1, doc_3]
 *		term_2 => [doc_1, doc_3, doc_5]
 *		term_3 => [doc_2]
 *
 *	See the index.h and storage.h headers for the details on the
 *	underlying in-memory and on-disk structures.
 *
 * Searching
 *
 *	The document search also involves tokenization of the query string
 *	with the filters applied.  The tokens are resolved to terms using
 *	the term index.  Then the reverse (term-document map) index is used
 *	to obtain the list of document IDs where the term is present.  The
 *	relevant counters are provided to the ranking algorithm to produce
 *	the relevance score for each document.
 *
 *	The nxs_index_search() API function and query handling is located
 *	in the src/query/ sub-directory.
 */

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "filters.h"
#include "rhashmap.h"
#include "storage.h"
#include "index.h"

static const char *default_filters[] = {
	"normalizer", "stopwords", "stemmer"
};

__dso_public nxs_t *
nxs_open(const char *basedir)
{
	nxs_t *nxs;
	const char *s;
	char *path = NULL;

	if ((s = getenv("NXS_LOG_LEVEL")) != NULL) {
		app_set_loglevel(s);
	}
	nxs = calloc(1, sizeof(nxs_t));
	if (nxs == NULL) {
		return NULL;
	}
	TAILQ_INIT(&nxs->index_list);

	/*
	 * Get the base directory and save the sanitized path.
	 */
	s = basedir ? basedir : getenv("NXS_BASEDIR");
	if (s == NULL || (nxs->basedir = realpath(s, NULL)) == NULL) {
		goto err;
	}
	if (asprintf(&path, "%s/data", nxs->basedir) == -1) {
		goto err;
	}
	if (mkdir(path, 0755) == -1 && errno != EEXIST) {
		goto err;
	}
	free(path);

	if (filters_sysinit(nxs) == -1) {
		goto err;
	}

	nxs->indexes = rhashmap_create(0, RHM_NONCRYPTO);
	if (nxs->indexes == NULL) {
		goto err;
	}
	return nxs;
err:
	nxs_close(nxs);
	free(path);
	return NULL;
}

__dso_public void
nxs_close(nxs_t *nxs)
{
	nxs_index_t *idx;

	while ((idx = TAILQ_FIRST(&nxs->index_list)) != NULL) {
		nxs_index_close(idx);
	}
	if (nxs->indexes) {
		rhashmap_destroy(nxs->indexes);
	}
	filters_sysfini(nxs);
	free(nxs->basedir);
	free(nxs->errmsg);
	free(nxs);
}

void
nxs_clear_error(nxs_t *nxs)
{
	free(nxs->errmsg);
	nxs->errmsg = NULL;
	nxs->errcode = NXS_ERR_SUCCESS;
}

void
nxs_error_checkpoint(nxs_t *nxs)
{
	/*
	 * There are error paths where error declaration is missing.
	 * In such case, just provide a general fatal error.
	 */
	if (!nxs->errcode) {
		nxs_decl_err(nxs, NXS_ERR_FATAL,
		    "internal error; last system errno", NULL);
	}
}

/*
 * _nxs_decl_error: set the index-level error message and log it.
 * If LOG_EMSG flag is set, then append the system-level error message.
 */
void
_nxs_decl_err(nxs_t *nxs, int level, const char *file, int line,
    const char *func, nxs_err_t code, const char *fmt, ...)
{
	const int error = errno;
	char *s = NULL, *msg = NULL;
	va_list ap;

	va_start(ap, fmt);
	(void)vasprintf(&s, fmt, ap);
	va_end(ap);

	_app_log(level, file, line, func, "%s", s);

	if (level & LOG_EMSG) {
		(void)asprintf(&msg, "%s: %s", s, strerror(error));
		free(s);
	} else {
		msg = s;
	}

	free(nxs->errmsg);
	nxs->errmsg = msg;
	nxs->errcode = code;
}

__dso_public nxs_err_t
nxs_get_error(const nxs_t *nxs, const char **errmsg)
{
	if (errmsg) {
		*errmsg = nxs->errmsg;
	}
	return nxs->errcode;
}

__dso_public nxs_index_t *
nxs_index_create(nxs_t *nxs, const char *name, nxs_params_t *params)
{
	nxs_params_t *def_params = NULL;
	const char **filters = NULL;
	nxs_index_t *idx = NULL;
	size_t filter_count;
	char *path;

	nxs_clear_error(nxs);

	/*
	 * Create the index directory.
	 */
	if (str_isalnumdu(name) == -1) {
		nxs_decl_errx(nxs, NXS_ERR_INVALID,
		    "invalid characters in index name", NULL);
		return NULL;
	}
	if (asprintf(&path, "%s/data/%s", nxs->basedir, name) == -1) {
		return NULL;
	}
	if (mkdir(path, 0755) == -1) {
		if (errno == EEXIST) {
			nxs_decl_err(nxs, NXS_ERR_EXISTS,
			    "index `%s' already exists", name);
			goto out;
		}
		nxs_decl_err(nxs, NXS_ERR_SYSTEM,
		    "could not create directory at %s", path);
		goto out;
	}
	free(path);
	path = NULL;

	/*
	 * Set the defaults.
	 */
	if (!params) {
		if ((def_params = nxs_params_create()) == NULL) {
			goto out;
		}
		params = def_params;
	}
	filters = nxs_params_get_strlist(params, "filters", &filter_count);
	if (filters == NULL && nxs_params_set_strlist(params, "filters",
	    default_filters, __arraycount(default_filters)) == -1) {
		goto out;
	}
	if (nxs_params_get_str(params, "algo") == NULL &&
	    nxs_params_set_str(params, "algo", NXS_DEFAULT_RANKING_ALGO) == -1) {
		goto out;
	}

	/* XXX Verify: Language must be a two letter ISO 639-1 code. */
	if (nxs_params_get_str(params, "lang") == NULL &&
	    nxs_params_set_str(params, "lang", NXS_DEFAULT_LANGUAGE) == -1) {
		goto out;
	}

	/*
	 * Write the index configuration.
	 */
	if (asprintf(&path, "%s/data/%s/params.db",
	    nxs->basedir, name) == -1) {
		goto out;
	}
	if (nxs_params_serialize(nxs, params, path) == -1) {
		goto out;
	}
	idx = nxs_index_open(nxs, name);
out:
	if (!idx) {
		nxs_error_checkpoint(nxs);
	}
	if (def_params) {
		nxs_params_release(def_params);
	}
	free(filters);
	free(path);
	return idx;
}

__dso_public int
nxs_index_destroy(nxs_t *nxs, const char *name)
{
	const char *idx_files[] = { "params.db", "nxsterms", "nxsdtmap", "" };
	const unsigned n = __arraycount(idx_files);
	int ec = 0, ret = -1;
	char *paths[n];

	if (str_isalnumdu(name) == -1) {
		nxs_decl_errx(nxs, NXS_ERR_INVALID,
		    "invalid characters in index name", NULL);
		return -1;
	}

	/* Initialize all paths. */
	for (unsigned i = 0; i < n; i++) {
		ec += asprintf(&paths[i], "%s/data/%s/%s",
		    nxs->basedir, name, idx_files[i]) == -1;
	}
	if (ec) {
		goto out;
	}

	/*
	 * Remove all index files, but skip the last entry.
	 */
	for (unsigned i = 0; i < n - 1 /* last entry is directory */; i++) {
		if (unlink(paths[i]) == -1) {
			nxs_decl_err(nxs, NXS_ERR_SYSTEM,
			    "could not remove `%s'", paths[i]);
			goto out;
		}
	}
	/*
	 * Finally, remove the directory stored in the last entry.
	 */
	if (rmdir(paths[n - 1]) == -1) {
		nxs_decl_err(nxs, NXS_ERR_SYSTEM,
		    "could not remove `%s'", paths[n - 1]);
		goto out;
	}
	ret = 0;
out:
	for (unsigned i = 0; i < n; i++) {
		free(paths[i]);
	}
	if (ret != 0) {
		nxs_error_checkpoint(nxs);
	}
	return ret;
}

static nxs_params_t *
index_get_params(nxs_t *nxs, const char *name)
{
	nxs_params_t *params;
	struct stat sb;
	char *path;

	if (asprintf(&path, "%s/data/%s/params.db", nxs->basedir, name) == -1)
		return NULL;
	if (stat(path, &sb) == -1 && errno == ENOENT) {
		nxs_decl_errx(nxs, NXS_ERR_MISSING,
		    "index `%s' does not exist", name);
		free(path);
		return NULL;
	}
	params = nxs_params_unserialize(nxs, path);
	free(path);
	return params;
}

__dso_public nxs_index_t *
nxs_index_open(nxs_t *nxs, const char *name)
{
	const size_t name_len = strlen(name);
	const char *algo_name;
	nxs_params_t *params;
	nxs_index_t *idx;
	char *path;
	int ret;

	nxs_clear_error(nxs);

	if (str_isalnumdu(name) == -1) {
		nxs_decl_errx(nxs, NXS_ERR_INVALID,
		    "invalid characters in index name", NULL);
		return NULL;
	}
	if (rhashmap_get(nxs->indexes, name, name_len)) {
		nxs_decl_errx(nxs, NXS_ERR_EXISTS,
		    "index `%s' is already open", name);
		return NULL;
	}

	idx = calloc(1, sizeof(nxs_index_t));
	if (idx == NULL) {
		nxs_error_checkpoint(nxs);
		return NULL;
	}
	idx->nxs = nxs;

	/*
	 * Load the index parameters.
	 */
	if ((params = index_get_params(nxs, name)) == NULL) {
		goto err;
	}
	idx->params = params;

	if ((algo_name = nxs_params_get_str(params, "algo")) == NULL) {
		nxs_decl_errx(nxs, NXS_ERR_FATAL,
		    "corrupted index params", NULL);
		goto err;
	}
	idx->algo = get_ranking_func_id(algo_name);

	/*
	 * Create the filter pipeline.
	 */
	idx->fp = filter_pipeline_create(nxs, params);
	if (idx->fp == NULL) {
		goto err;
	}

	/*
	 * Open the terms index.
	 */
	if (asprintf(&path, "%s/data/%s/%s",
	    nxs->basedir, name, "nxsterms") == -1) {
		goto err;
	}
	ret = idx_terms_open(idx, path);
	free(path);
	if (ret == -1) {
		goto err;
	}

	/*
	 * Open the document-term index.
	 */
	if (asprintf(&path, "%s/data/%s/%s",
	    nxs->basedir, name, "nxsdtmap") == -1) {
		goto err;
	}
	ret = idx_dtmap_open(idx, path);
	free(path);
	if (ret == -1) {
		goto err;
	}
	idx->name = strdup(name);
	rhashmap_put(nxs->indexes, name, name_len, idx);
	TAILQ_INSERT_TAIL(&nxs->index_list, idx, entry);
	return idx;
err:
	nxs_error_checkpoint(nxs);
	nxs_index_close(idx);
	return NULL;
}

__dso_public nxs_params_t *
nxs_index_get_params(nxs_index_t *idx)
{
	ASSERT(idx->params);
	return idx->params;
}

__dso_public void
nxs_index_close(nxs_index_t *idx)
{
	nxs_t *nxs = idx->nxs;

	if (idx->name) {
		TAILQ_REMOVE(&nxs->index_list, idx, entry);
		rhashmap_del(nxs->indexes, idx->name, strlen(idx->name));
		free(idx->name);
	}
	if (idx->fp) {
		filter_pipeline_destroy(idx->fp);
	}
	if (idx->params) {
		nxs_params_release(idx->params);
	}
	idx_dtmap_close(idx);
	idx_terms_close(idx);
	free(idx);
}

__dso_public int
nxs_index_add(nxs_index_t *idx, nxs_doc_id_t doc_id,
    const char *text, size_t len)
{
	tokenset_t *tokens;
	int ret = -1;

	nxs_clear_error(idx->nxs);
	if (doc_id == 0) {
		nxs_decl_errx(idx->nxs, NXS_ERR_INVALID,
		    "document ID must be non-zero", NULL);
		return -1;
	}
	if (doc_id > UINT32_MAX) {
		nxs_decl_errx(idx->nxs, NXS_ERR_INVALID,
		    "document ID must be not greater than UINT32_MAX", NULL);
		return -1;
	}

	/*
	 * Check whether the document already exists.
	 */
	if (idxdoc_lookup(idx, doc_id)) {
		nxs_decl_errx(idx->nxs, NXS_ERR_EXISTS,
		    "document %"PRIu64" is already indexed", doc_id);
		return -1;
	}

	/*
	 * Tokenize and resolve tokens to terms.
	 */
	if ((tokens = tokenize(idx->fp, idx->params, text, len)) == NULL) {
		nxs_decl_errx(idx->nxs, NXS_ERR_FATAL,
		    "tokenizer failed", NULL);
		return -1;
	}
	if (tokens->count == 0) {
		nxs_decl_errx(idx->nxs, NXS_ERR_MISSING,
		    "the text is empty or no meaningful tokens found", NULL);
		goto out;
	}
	tokenset_resolve(tokens, idx, TOKENSET_STAGE);

	/*
	 * Add new terms (if any).
	 */
	if (idx_terms_add(idx, tokens) == -1) {
		goto out;
	}
	ASSERT(TAILQ_EMPTY(&tokens->staging));

	/*
	 * Add document.
	 */
	if (idx_dtmap_add(idx, doc_id, tokens) == -1) {
		goto out;
	}
	ret = 0;
out:
	tokenset_destroy(tokens);
	if (ret != 0) {
		nxs_error_checkpoint(idx->nxs);
	}
	return ret;
}

/*
 * nxs_index_remove: remove the document from the index.
 */
__dso_public int
nxs_index_remove(nxs_index_t *idx, nxs_doc_id_t doc_id)
{
	if (idx_dtmap_sync(idx) == -1 || idx_dtmap_remove(idx, doc_id) == -1) {
		nxs_error_checkpoint(idx->nxs);
		return -1;
	}
	return 0;
}
