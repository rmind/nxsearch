/*
 * Copyright (c) 2022-2024 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Searching.
 *
 * - Parses the query into the intermediate representation (IR);
 * - Runs the query logic processing the IR to find the matching documents;
 * - Scores the documents.
 *
 * Consider the following query:
 *
 *     textbook AND (C OR C++ OR Python) AND
 *     (Linux OR UNIX) AND NOT (C# OR Java)
 *
 * It will be represented as:
 *
 *     (ANDNOT
 *       (AND
 *         textbook
 *         (OR C C++ Python)
 *         (OR Linux Unix))
 *       (OR C# Java))
 *
 * Each term translates to a bitmap of documents referred by T.doc_bitmap.
 * We walk recursively to produce the final bitmap of matching documents.
 * Pseudo-code:
 *
 *     doc_bitmap = roaring_bitmap_andnot([
 *         roaring_bitmap_and_many([
 *             T1.doc_bitmap,
 *             roaring_bitmap_or_many([
 *                 T2.doc_bitmap, T3.doc_bitmap, T4.doc_bitmap
 *             ]),
 *             roaring_bitmap_or_many([
 *                 T5.doc_bitmap, T6.doc_bitmap
 *             ]),
 *         ]),
 *         roaring_bitmap_or_many([T7.doc_bitmap, T8.doc_bitmap])
 *     ])
 *
 * The scores get summed.  Pseudo-code:
 *
 *     doc_scores = {}
 *     for doc_id in doc_bitmap:
 *         for term_id in terms:
 *             if doc_id not in term->doc_bitmap:
 *                 continue
 *             score = rank(term_id, doc_id)
 *             doc_scores[doc_id] += score
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "index.h"
#include "expr.h"
#define	__NXS_PARSER_PRIVATE
#include "query.h"
#include "utils.h"

/* Query nesting limit to prevent deep recursion. */
#define	NXS_QUERY_RLIMIT	(100)

typedef struct {
	uint64_t		limit;
	ranking_algo_t		algo;
	unsigned		tflags;
} search_params_t;

static int
get_search_params(nxs_index_t *idx, nxs_params_t *params, search_params_t *sp)
{
	const char *s;
	bool fl;

	/*
	 * Set some defaults.
	 */
	memset(sp, 0, sizeof(search_params_t));
	sp->limit = NXS_DEFAULT_RESULTS_LIMIT;
	sp->tflags = TOKENSET_FUZZYMATCH;
	sp->algo = idx->algo;

	if (!params) {
		return 0;
	}

	if (nxs_params_get_uint(params, "limit", &sp->limit) == 0 &&
	    (sp->limit == 0 || sp->limit > UINT_MAX)) {
		nxs_decl_errx(idx->nxs, NXS_ERR_INVALID,
		    "invalid limit", NULL);
		return -1;
	}
	if ((s = nxs_params_get_str(params, "algo")) != NULL &&
	    (sp->algo = get_ranking_func_id(s)) == INVALID_ALGO) {
		nxs_decl_errx(idx->nxs, NXS_ERR_INVALID,
		    "invalid algorithm", NULL);
		return -1;
	}
	if (nxs_params_get_bool(params, "fuzzymatch", &fl) == 0 && !fl) {
		sp->tflags &= ~TOKENSET_FUZZYMATCH;
	}
	return 0;
}

/*
 * get_expr_bitmap: recursive process AND/OR/NOT expressions and produce
 * the resulting document bitmap.
 */
static roaring64_bitmap_t *
get_expr_bitmap(nxs_index_t *idx, expr_t *expr, unsigned r)
{
	roaring64_bitmap_t *result;
	expr_t *subexpr;

	ASSERT(expr != NULL);

	if (r > NXS_QUERY_RLIMIT) {
		nxs_decl_errx(idx->nxs, NXS_ERR_LIMIT,
		    "query nesting limit reached (%u levels)",
		    NXS_QUERY_RLIMIT, NULL);
		return NULL;
	}

	if (expr->type == EXPR_VAL_TOKEN) {
		const token_t *token = expr->token;

		if (token) {
			const idxterm_t *term = token->idxterm;
			return roaring64_bitmap_copy(term->doc_bitmap);
		}
		return roaring64_bitmap_create();
	}
	ASSERT(expr->nitems > 0);

	subexpr = expr->elements[0];
	if ((result = get_expr_bitmap(idx, subexpr, r + 1)) == NULL) {
		return NULL;
	}

	for (unsigned i = 1; i < expr->nitems; i++) {
		roaring64_bitmap_t *elm;

		subexpr = expr->elements[i];
		if ((elm = get_expr_bitmap(idx, subexpr, r + 1)) == NULL) {
			roaring64_bitmap_free(result);
			return NULL;
		}

		switch (expr->type) {
		case EXPR_OP_AND:
			roaring64_bitmap_and_inplace(result, elm);
			break;
		case EXPR_OP_OR:
			roaring64_bitmap_or_inplace(result, elm);
			break;
		case EXPR_OP_NOT:
			roaring64_bitmap_andnot_inplace(result, elm);
			break;
		default:
			abort();
		}
		roaring64_bitmap_free(elm);
	}
	return result;
}

static query_t *
construct_query(nxs_index_t *idx, const char *query, size_t len __unused,
    search_params_t *sp)
{
	query_t *q;

	if ((q = query_create(idx)) == NULL) {
		return NULL;
	}

	/* Parse the query. */
	if (query_parse(q, query) == -1) {
		nxs_decl_err(idx->nxs, NXS_ERR_FATAL,
		    "query_parse() failed", NULL);
		goto err;
	}
	if (q->error) {
		nxs_decl_errx(idx->nxs, NXS_ERR_INVALID,
		    "query failed with %s", query_get_error(q));
		goto err;
	}

	/* Resolve the tokens to terms. */
	if (query_prepare(q, sp->tflags) == -1) {
		nxs_decl_errx(idx->nxs, NXS_ERR_FATAL,
		    "query_prepare() failed", NULL);
		goto err;
	}
	return q;
err:
	query_destroy(q);
	return NULL;
}

static int
run_query_logic(query_t *query, ranking_func_t rank, nxs_resp_t *resp)
{
	nxs_index_t *idx = query->idx;
	tokenset_t *tokens = query->tokens;
	roaring64_iterator_t *bm_iter;
	roaring64_bitmap_t *doc_bitmap;
	int ret = -1;

	/*
	 * If there are no expressions or meaningful tokens (terms in use),
	 * then then just return without an error, since such search merely
	 * produces an empty search results.
	 */
	if (!query->root || tokens->count == 0) {
		return 0;
	}

	/*
	 * Process the expression logic and get the resulting bitmap.
	 */
	doc_bitmap = get_expr_bitmap(idx, query->root, 0);
	if (!doc_bitmap) {
		return -1;
	}
	bm_iter = roaring64_iterator_create(doc_bitmap);
	while (roaring64_iterator_has_value(bm_iter)) {
		const nxs_doc_id_t doc_id = roaring64_iterator_value(bm_iter);
		token_t *token;

		TAILQ_FOREACH(token, &tokens->list, entry) {
			const idxterm_t *term = token->idxterm;
			idxdoc_t *doc;
			float score;

			ASSERT(term != NULL);

			/*
			 * Skip if this term is not used in the document.
			 */
			if (!roaring64_bitmap_contains(
			    term->doc_bitmap, doc_id)) {
				continue;
			}

			/*
			 * Lookup the document and compute the score.
			 */
			if ((doc = idxdoc_lookup(idx, doc_id)) == NULL) {
				goto out;
			}
			if ((score = rank(idx, term, doc)) < 0) {
				/*
				 * Negative value means no score to be given.
				 */
				continue;
			}
			if (nxs_resp_addresult(resp, doc, score) == -1) {
				goto out;
			}
		}
		roaring64_iterator_advance(bm_iter);
	}
	ret = 0;
out:
	roaring64_iterator_free(bm_iter);
	roaring64_bitmap_free(doc_bitmap);
	return ret;
}

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
	search_params_t sp;
	ranking_func_t rank;
	query_t *q = NULL;
	int err = -1;

	nxs_clear_error(idx->nxs);

	/* Prepare the search parameters. */
	if (get_search_params(idx, params, &sp) == -1) {
		return NULL;
	}

	/* Determine the ranking algorithm. */
	rank = get_ranking_func(sp.algo);
	ASSERT(rank != NULL);

	/*
	 * Sync the latest updates to the index.
	 */
	if (idx_terms_sync(idx) == -1 ||
	    idx_dtmap_sync(idx, DTMAP_PARTIAL_SYNC) == -1) {
		return NULL;
	}

	/*
	 * Parse the query and construct the intermediate representation.
	 */
	if ((q = construct_query(idx, query, len, &sp)) == NULL) {
		goto out;
	}

	/*
	 * Create the response object and run the query logic which
	 * performs the searching and scoring of the documents.
	 */
	if ((resp = nxs_resp_create(sp.limit)) == NULL) {
		goto out;
	}
	if (run_query_logic(q, rank, resp) == -1) {
		goto out;
	}
	nxs_resp_build(resp);
	err = 0;
out:
	if (err && resp) {
		nxs_resp_release(resp);
		resp = NULL;
	}
	if (q) {
		query_destroy(q);
	}
	return resp;
}
