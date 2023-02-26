/*
 * Copyright (c) 2023 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "index.h"
#include "tokenizer.h"
#include "deque.h"
#include "expr.h"
#include "utils.h"

////////////////////////////////////////////////////////////////////////////

query_t *
query_create(nxs_index_t *idx)
{
	query_t *q;

	if ((q = calloc(1, sizeof(query_t))) == NULL) {
		return NULL;
	}
	if ((q->tokens = tokenset_create()) == NULL) {
		free(q);
		return NULL;
	}
	q->idx = idx;
	return q;
}

void
query_destroy(query_t *q)
{
	if (q->root) {
		expr_destroy(q->root);
	}
	tokenset_destroy(q->tokens);
	free(q);
}

#if 0
void
query_prepare(query_t *q, unsigned flags)
{
	tokenset_resolve(q->tokens, q->idx, TOKENSET_TRIM | flags);
}
#endif

////////////////////////////////////////////////////////////////////////////

expr_t *
expr_create(query_t *q, expr_type_t type)
{
	expr_t *expr;

	expr = calloc(1, sizeof(expr_t));
	if (!expr) {
		return NULL;
	}
	expr->type = type;
	expr->query = q;

	if (expr->type != EXPR_VAL_TOKEN) {
		expr->elements = deque_create(0, 0);
		if (!expr->elements) {
			free(expr);
			return NULL;
		}
	}
	return expr;
}

#if 0
int
expr_set_token(expr_t *expr, const char *value, size_t len)
{
	query_t *q = expr->query;
	filter_action_t action;
	token_t *token;

	ASSERT(expr->type == EXPR_VAL_TOKEN);

	/*
	 * Create a new token and run the filter pipeline.
	 */
	token = token_create(value, len);
	if (__predict_false(token == NULL)) {
		return -1;
	}

	action = filter_pipeline_run(q->idx->fp, &token->buffer);
	if (__predict_false(action != FILT_MUTATION)) {
		ASSERT(action == FILT_DISCARD || action == FILT_ERROR);
		token_destroy(token);
		if (action == FILT_ERROR) {
			return -1;
		}
		return 0;
	}
	expr->token = token;
	return 0;
}
#endif

int
expr_add_element(expr_t *expr, expr_t *elm)
{
	ASSERT(expr->type != EXPR_VAL_TOKEN);
	return deque_push(expr->elements, elm);
}

void
expr_destroy(expr_t *expr)
{
	deque_t *gc;

	/*
	 * G/C list for the expressions.  Begin with itself.
	 */
	gc = deque_create(0, 0);
	deque_push(gc, expr);

	/*
	 * Deep-walk and G/C all expressions.
	 */
	while ((expr = deque_pop_back(gc)) != NULL) {
		unsigned nitems;

		if (expr->type == EXPR_VAL_TOKEN) {
			ASSERT(expr->token);
			free(expr);
			continue;
		}

		nitems = deque_count(expr->elements);
		for (unsigned i = 0; i < nitems; i++) {
			expr_t *subexpr = deque_get(expr->elements, i);
			deque_push(gc, subexpr);
		}
		deque_destroy(expr->elements);
		free(expr);
	}
	deque_destroy(gc);
}
