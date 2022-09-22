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
 */

#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "filters.h"
#include "rhashmap.h"
#include "storage.h"
#include "index.h"

__dso_public nxs_t *
nxs_create(const char *basedir)
{
	nxs_t *nxs;
	const char *s;
	char *path = NULL;

	nxs = calloc(1, sizeof(nxs_t));
	if (nxs == NULL) {
		return NULL;
	}
	TAILQ_INIT(&nxs->index_list);

	s = basedir ? basedir : getenv("NXS_BASEDIR");
	if (s && (nxs->basedir = strdup(s)) == NULL) {
		goto err;
	}
	if (asprintf(&path, "%s/data", nxs->basedir) == -1) {
		goto err;
	}
	if (mkdir(path, 0644) == -1 && errno != EEXIST) {
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
	nxs_destroy(nxs);
	free(path);
	return NULL;
}

__dso_public void
nxs_destroy(nxs_t *nxs)
{
	nxs_index_t *idx;

	while ((idx = TAILQ_FIRST(&nxs->index_list)) != NULL) {
		nxs_index_close(nxs, idx);
	}
	if (nxs->indexes) {
		rhashmap_destroy(nxs->indexes);
	}
	filters_sysfini(nxs);
	free(nxs->basedir);
	free(nxs);
}

__dso_public nxs_index_t *
nxs_index_create(nxs_t *nxs, const char *name)
{
	char *path;

	/*
	 * Create the index directory.
	 */
	if (asprintf(&path, "%s/data/%s", nxs->basedir, name) == -1) {
		return NULL;
	}
	if (mkdir(path, 0644) == -1) {
		free(path);
		return NULL;
	}
	free(path);

	/*
	 * TODO: Write the index configuration.
	 */

	return nxs_index_open(nxs, name);
}

__dso_public nxs_index_t *
nxs_index_open(nxs_t *nxs, const char *name)
{
	const char *filters[] = {
		// TODO: Implement index parameters
		"normalizer", "stemmer"
	};
	const size_t name_len = strlen(name);
	nxs_index_t *idx;
	char *path;
	int ret;

	if (rhashmap_get(nxs->indexes, name, name_len)) {
		errno = EEXIST;
		return NULL;
	}

	idx = calloc(1, sizeof(nxs_index_t));
	if (idx == NULL) {
		return NULL;
	}
	if (idxterm_sysinit(idx) == -1) {
		nxs_index_close(nxs, idx);
		return NULL;
	}

	idx->fp = filter_pipeline_create(nxs,
	    "en" /* XXX */, filters, __arraycount(filters));
	if (idx->fp == NULL) {
		nxs_index_close(nxs, idx);
		return NULL;
	}

	/*
	 * Open the terms index.
	 */
	if (asprintf(&path, "%s/data/%s/%s",
	    nxs->basedir, name, "nxsterms") == -1) {
		return NULL;
	}
	ret = idx_terms_open(idx, path);
	free(path);
	if (ret == -1) {
		nxs_index_close(nxs, idx);
		return NULL;
	}

	/*
	 * Open the document-term index.
	 */
	if (asprintf(&path, "%s/data/%s/%s",
	    nxs->basedir, name, "nxsdtmap") == -1) {
		return NULL;
	}
	ret = idx_dtmap_open(idx, path);
	free(path);
	if (ret == -1) {
		nxs_index_close(nxs, idx);
		return NULL;
	}

	idx->name = strdup(name);
	rhashmap_put(nxs->indexes, name, name_len, idx);
	TAILQ_INSERT_TAIL(&nxs->index_list, idx, entry);
	return idx;
}

__dso_public void
nxs_index_close(nxs_t *nxs, nxs_index_t *idx)
{
	if (idx->name) {
		TAILQ_REMOVE(&nxs->index_list, idx, entry);
		rhashmap_del(nxs->indexes, idx->name, strlen(idx->name));
		free(idx->name);
	}
	if (idx->fp) {
		filter_pipeline_destroy(idx->fp);
	}
	idx_dtmap_close(idx);
	idx_terms_close(idx);
	idxterm_sysfini(idx);
	free(idx);
}

__dso_public int
nxs_index_add(nxs_index_t *idx, uint64_t doc_id, const char *text, size_t len)
{
	tokenset_t *tokens;
	int ret = -1;

	/* Check whether the document already exists.
	*/
	if (idxdoc_lookup(idx, doc_id)) {
		errno = EEXIST;
		return -1;
	}

	/*
	 * Tokenize and resolve tokens to terms.
	 */
	if ((tokens = tokenize(idx->fp, text, len)) == NULL) {
		return -1;
	}
	tokenset_resolve(tokens, idx, true);

	/*
	 * Add new terms.
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
	return ret;
}

/*
 * nxs_index_search: perform  a search query on the given index.
 *
 * => Returns the response object (which must be released by the caller).
 */
__dso_public nxs_resp_t *
nxs_index_search(nxs_index_t *idx, const char *query, size_t len)
{
	nxs_resp_t *resp = NULL;
	ranking_func_t rank;
	tokenset_t *tokens;
	token_t *token;
	char *text;
	int err = -1;

	/*
	 * Sync the latest updates to the index.
	 */
	if (idx_terms_sync(idx) == -1 || idx_dtmap_sync(idx) == -1) {
		return NULL;
	}

	/* Determine the ranking algorithm. */
	switch (idx->algo) {
	case TF_IDF:
		rank = tf_idf;
		break;
	case BM25:
		rank = bm25;
		break;
	default:
		abort();
	}

	/*
	 * Tokenize and resolve tokens to terms.
	 */
	if ((text = strdup(query)) == NULL) {
		return NULL;
	}
	if ((tokens = tokenize(idx->fp, text, len)) == NULL) {
		free(text);
		return NULL;
	}
	tokenset_resolve(tokens, idx, false);
	free(text);

	/*
	 * Lookup the documents given the terms.
	 */
	if ((resp = nxs_resp_create()) == NULL) {
		goto out;
	}
	TAILQ_FOREACH(token, &tokens->list, entry) {
		roaring_uint32_iterator_t *bm_iter;
		idxterm_t *term;

		if ((term = token->idxterm) == NULL) {
			/* The term is not in the index: just skip. */
			continue;
		}

		bm_iter = roaring_create_iterator(term->doc_bitmap);
		while (bm_iter->has_value) {
			const nxs_doc_id_t doc_id = bm_iter->current_value;
			idxdoc_t *doc;
			float score;

			/*
			 * Lookup the document and compute its score.
			 */
			if ((doc = idxdoc_lookup(idx, doc_id)) == NULL) {
				goto out;
			}
			score = rank(idx, term, doc);
			if (nxs_resp_addresult(resp, doc, score) == -1) {
				goto out;
			}
			roaring_advance_uint32_iterator(bm_iter);
		}
		roaring_free_uint32_iterator(bm_iter);
	}
	nxs_resp_build(resp);
	err = 0;
out:
	if (err && resp) {
		nxs_resp_release(resp);
		resp = NULL;
	}
	tokenset_destroy(tokens);
	return resp;
}
