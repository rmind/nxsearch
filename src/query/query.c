/*
 * Copyright (c) 2023 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdlib.h>
#include <string.h>

#include "index.h"
#include "tokenizer.h"
#include "expr.h"
#define	__NXS_PARSER_PRIVATE
#include "query.h"
#include "utils.h"

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
	if (q->errmsg) {
		free(q->errmsg);
	}
	tokenset_destroy(q->tokens);
	free(q);
}

void
query_set_error(query_t *q)
{
	const lexer_t *ctx = &q->lexer;
	unsigned offset = (uintptr_t)ctx->token - (uintptr_t)ctx->cur_line;

	ASSERT(!q->error);
	ASSERT(q->errmsg == NULL);

	asprintf(&q->errmsg, "syntax error near %u:%u: \"%.50s ...\"",
	    ctx->line, offset, ctx->token);
	q->error = true;
}

const char *
query_get_error(query_t *q)
{
	if (!q->error) {
		// No error
		return NULL;
	}
	if (!q->errmsg) {
		// Error without a message
		return "out of memory";
	}
	return q->errmsg;
}

int
query_prepare(query_t *q, unsigned flags)
{
	filter_pipeline_t *fp = q->idx->fp;
	deque_t *iter;
	expr_t *expr;
	int ret = -1;

	iter = deque_create(0, 0);
	deque_push(iter, q->root);

	/*
	 * Deep-walk the expressions and obtain the tokens.
	 */
	while ((expr = deque_pop_back(iter)) != NULL) {
		if (EXPR_IS_OPERATOR(expr->type)) {
			for (unsigned i = 0; i < expr->nitems; i++) {
				expr_t *subexpr = expr->elements[i];
				deque_push(iter, subexpr);
			}
			continue;
		}
		ASSERT(expr->nitems == 0);

		/*
		 * Tokenize the value; if there is no term in use,
		 * then the expr->token will remain NULL.
		 */
		if (tokenize_value(fp, q->tokens, expr->value,
		    strlen(expr->value), &expr->token) == -1) {
			goto err;
		}
	}

	/* Resolve tokens to terms, removing those not in use. */
	tokenset_resolve(q->tokens, q->idx, TOKENSET_TRIM | flags);
	ret = 0;
err:
	deque_destroy(iter);
	return ret;
}
