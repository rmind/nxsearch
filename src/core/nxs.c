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
 *	See the storage.h and index.h headers for the details on the
 *	underlying in-memory and on-disk structures.
 */

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

	nxs = calloc(1, sizeof(nxs_t));
	if (nxs == NULL) {
		return NULL;
	}
	s = basedir ? basedir : getenv("NXS_BASEDIR");
	if (s && (nxs->basedir = strdup(s)) == NULL) {
		goto err;
	}
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
nxs_index_open(nxs_t *nxs, const char *name)
{
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
	idx_terms_close(idx);
	idxterm_sysfini(idx);
	free(idx);
}

#if 0

int
nxs_index_add(nxs_t *nxs, const char *text, size_t len)
{
	/*
	 * Tokenize.
	 */

	/*
	 * Resolve tokens to terms.
	 */

	/*
	 * Add new terms.
	 */

	/*
	 * Add document.
	 */

	return 0;
}

int
nxs_index_search(nxs_t *nxs, const char *query, size_t len)
{
	/*
	 * Tokenize.
	 */

	/*
	 * Resolve tokens to terms.
	 */

	/*
	 * Lookup the documents given the terms.
	 */

	/*
	 * Rank the documents.
	 */

	/*
	 * Return the document IDs.
	 */

	return 0;
}

#endif
