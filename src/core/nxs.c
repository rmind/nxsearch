/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * nxsearch: a full-text search engine.
 *
 * Indexes
 *
 *	There are two data files which form the index:
 *
 *	- Terms list (nxsterms.db) which contains the full list of all
 *	indexed terms.  The term ID is determined by the term order in
 *	the index starting from 1.
 *
 *	- Document-term mapping (nxsdtmap.db) which contains the mappings
 *	of document IDs to the set of terms from the aforementioned list,
 *	referenced by their IDs.  The term IDs, associated with a document,
 *	are accompanied by count which represents the term occurrences in
 *	the document.
 *
 *	Using the above two, an in-memory *reverse* index is built:
 *
 *		term_1 => [doc_1, doc_3]
 *		term_2 => [doc_1, doc_3, doc_5]
 *		term_3 => [doc_2]
 *
 *	See the index.h and storage.h headers for the details on the
 *	underlying in-memory and on-disk structures.
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

nxs_t *
nxs_create(const char *basedir)
{
	nxs_t *nxs;
	const char *s;
	char *path = NULL;

	nxs = calloc(1, sizeof(nxs_t));
	if (nxs == NULL) {
		return NULL;
	}

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
	if (filters_builtin_sysinit(nxs) == -1) {
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

void
nxs_destroy(nxs_t *nxs)
{
	if (nxs->indexes) {
		rhashmap_destroy(nxs->indexes);
	}
	filters_sysfini(nxs);
	free(nxs->basedir);
	free(nxs);
}

fts_index_t *
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

fts_index_t *
nxs_index_open(nxs_t *nxs, const char *name)
{
	const char *filters[] = {
		// TODO: Implement index parameters
		"normalizer", "stemmer"
	};
	const size_t name_len = strlen(name);
	fts_index_t *idx;
	char *path;
	int ret;

	if (rhashmap_get(nxs->indexes, name, name_len)) {
		errno = EEXIST;
		return NULL;
	}

	idx = calloc(1, sizeof(fts_index_t));
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
	return idx;
}

void
nxs_index_close(nxs_t *nxs, fts_index_t *idx)
{
	if (idx->name) {
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

int
nxs_index_add(fts_index_t *idx, uint64_t doc_id, const char *text, size_t len)
{
	tokenset_t *tokens;
	int ret = -1;

	/*
	 * Tokenize and resolve tokens to terms.
	 */
	if ((tokens = tokenize(idx->fp, text, len)) == NULL) {
		return -1;
	}
	idxterm_resolve_tokens(idx, tokens, true);

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

static nxs_result_entry_t *
prepare_doc_entry(nxs_results_t *results, rhashmap_t *doc_map,
    const idxdoc_t *doc, float score)
{
	nxs_result_entry_t *entry;

	entry = rhashmap_get(doc_map, &doc->id, sizeof(doc_id_t));
	if (entry == NULL) {
		if ((entry = calloc(1, sizeof(nxs_result_entry_t))) == NULL) {
			return NULL;
		}
		rhashmap_put(doc_map, &doc->id, sizeof(doc_id_t), entry);

		entry->doc_id = doc->id;
		entry->next = results->entries;
		results->entries = entry;
		results->count++;
	}

	entry->score += score;
	return entry;
}

nxs_results_t *
nxs_index_search(fts_index_t *idx, const char *query, size_t len)
{
	nxs_results_t *results = NULL;
	tokenset_t *tokens;
	rhashmap_t *doc_map;
	token_t *token;
	char *text;
	int err = -1;

	/*
	 * Sync the latest updates to the index.
	 */
	if (idx_terms_sync(idx) == -1 || idx_dtmap_sync(idx) == -1) {
		return NULL;
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
	idxterm_resolve_tokens(idx, tokens, false);
	free(text);

	doc_map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	if (doc_map == NULL) {
		goto out;
	}
	if ((results = calloc(1, sizeof(nxs_results_t))) == NULL) {
		goto out;
	}

	/*
	 * Lookup the documents given the terms.
	 */
	TAILQ_FOREACH(token, &tokens->list, entry) {
		roaring_uint32_iterator_t *bm_iter;
		idxterm_t *term;

		if ((term = token->idxterm) == NULL) {
			/* The term is not in the index: just skip. */
			continue;
		}

		bm_iter = roaring_create_iterator(term->doc_bitmap);
		while (bm_iter->has_value) {
			const doc_id_t doc_id = bm_iter->current_value;
			idxdoc_t *doc;
			float score;

			/*
			 * Lookup the document and compute its score.
			 */
			if ((doc = idxdoc_lookup(idx, doc_id)) == NULL) {
				goto out;
			}
			score = tf_idf(idx, term, doc);
			if (!prepare_doc_entry(results, doc_map, doc, score)) {
				goto out;
			}
			roaring_advance_uint32_iterator(bm_iter);
		}
		roaring_free_uint32_iterator(bm_iter);
	}
	err = 0;
out:
	if (err && results) {
		nxs_results_release(results);
	}
	if (doc_map) {
		rhashmap_destroy(doc_map);
	}
	tokenset_destroy(tokens);
	return results;
}

void
nxs_results_release(nxs_results_t *results)
{
	if (results) {
		nxs_result_entry_t *entry = results->entries;

		while (entry) {
			nxs_result_entry_t *next = entry->next;
			free(entry);
			entry = next;
		}
	}
	free(results);
}
