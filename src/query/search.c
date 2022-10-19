/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "index.h"
#include "utils.h"

/*
 * nxs_index_search: perform  a search query on the given index.
 *
 * => Returns the response object (which must be released by the caller).
 */
__dso_public nxs_resp_t *
nxs_index_search(nxs_index_t *idx, nxs_params_t *params,
    const char *query, size_t len)
{
	nxs_resp_t *resp = NULL;
	ranking_algo_t algo;
	ranking_func_t rank;
	tokenset_t *tokens;
	token_t *token;
	uint64_t limit;
	char *text;
	int err = -1;

	nxs_clear_error(idx->nxs);

	/*
	 * Check the parameters and set some defaults.
	 */
	limit = NXS_DEFAULT_RESULTS_LIMIT;
	algo = idx->algo;

	if (params) {
		const char *algo_name;

		if (nxs_params_get_uint(params, "limit", &limit) == 0 &&
		    (limit == 0 || limit > UINT_MAX)) {
			nxs_decl_errx(idx->nxs, NXS_ERR_INVALID,
			    "invalid limit", NULL);
			return NULL;
		}
		if ((algo_name = nxs_params_get_str(params, "algo")) != NULL &&
		    (algo = get_ranking_func_id(algo_name)) == INVALID_ALGO) {
			nxs_decl_errx(idx->nxs, NXS_ERR_INVALID,
			    "invalid algorithm", NULL);
			return NULL;
		}
	}

	/* Determine the ranking algorithm. */
	rank = get_ranking_func(algo);
	ASSERT(rank != NULL);

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
		nxs_decl_errx(idx->nxs, NXS_ERR_FATAL,
		    "tokenizer failed", NULL);
		free(text);
		return NULL;
	}
	free(text);
	if (tokens->count == 0) {
		nxs_decl_errx(idx->nxs, NXS_ERR_MISSING,
		    "the query is empty or has no meaningful tokens", NULL);
		goto out;
	}
	tokenset_resolve(tokens, idx, false);

	/*
	 * Lookup the documents given the terms.
	 */
	if ((resp = nxs_resp_create(limit)) == NULL) {
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
