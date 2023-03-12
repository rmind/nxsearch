/*
 * Copyright (c) 2023 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _EXPR_H_
#define _EXPR_H_

struct token;

typedef enum {
	// Values:
	EXPR_VAL_TOKEN,
	// Operators:
	EXPR_OP_AND,
	EXPR_OP_OR,
	EXPR_OP_NOT,
} expr_type_t;

#define	EXPR_IS_OPERATOR(t)	((t) != EXPR_VAL_TOKEN)

typedef struct expr {
	expr_type_t		type;

	// EXPR_VAL_TOKEN:
	char *			value;
	struct token *		token;

	// EXPR_IS_OPERATOR:
	unsigned		nitems;
	struct expr *		elements[];
} expr_t;

expr_t *	expr_create(expr_type_t, unsigned);
void		expr_destroy(expr_t *);

expr_t *	expr_create_token(char *);
expr_t *	expr_create_operator(expr_type_t, expr_t *, expr_t *);

#endif
