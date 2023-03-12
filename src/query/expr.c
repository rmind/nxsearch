/*
 * Copyright (c) 2023 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "deque.h"
#include "expr.h"
#include "utils.h"

expr_t *
expr_create(expr_type_t type, unsigned n)
{
	const size_t len = offsetof(expr_t, elements[n]);
	expr_t *expr;

	ASSERT(n || type == EXPR_VAL_TOKEN);

	if ((expr = calloc(1, len)) == NULL) {
		return NULL;
	}
	expr->type = type;
	expr->nitems = n;
	return expr;
}

/*
 * expr_create_token: create an expression with a given token value.
 *
 * => The given string will be consumed and released on expr_destroy().
 */
expr_t *
expr_create_token(char *value)
{
	expr_t *expr;

	if ((expr = expr_create(EXPR_VAL_TOKEN, 0)) == NULL) {
		return NULL;
	}
	expr->value = value;
	return expr;
}

expr_t *
expr_create_operator(expr_type_t type, expr_t *e1, expr_t *e2)
{
	expr_t *expr;

	if ((expr = expr_create(type, 2)) == NULL) {
		return NULL;
	}
	ASSERT(EXPR_IS_OPERATOR(expr->type));
	expr->elements[0] = e1;
	expr->elements[1] = e2;
	return expr;
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
		if (expr->type == EXPR_VAL_TOKEN) {
			ASSERT(expr->nitems == 0);
			free(expr->value);
			free(expr);
			continue;
		}
		for (unsigned i = 0; i < expr->nitems; i++) {
			expr_t *subexpr = expr->elements[i];

			if (subexpr) {
				deque_push(gc, subexpr);
			}
		}
		free(expr);
	}
	deque_destroy(gc);
}
