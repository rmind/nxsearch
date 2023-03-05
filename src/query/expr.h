/*
 * Copyright (c) 2023 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _EXPR_H_
#define _EXPR_H_

typedef struct query query_t;
typedef struct expr expr_t;

typedef enum {
	// Values:
	EXPR_VAL_TOKEN,
	// Operations:
	EXPR_OP_AND,
	EXPR_OP_OR,
	EXPR_OP_NOT,
} expr_type_t;

struct query {
	nxs_index_t *		idx;
	tokenset_t *		tokens;
	expr_t *		root;
};

struct expr {
	expr_type_t		type;
	query_t *		query;
	union {
		token_t *	token;
		deque_t *	elements;
	};
};

query_t *	query_create(nxs_index_t *);
void		query_destroy(query_t *);
void		query_prepare(query_t *, unsigned);

expr_t *	expr_create(query_t *, expr_type_t);
int		expr_set_token(expr_t *, const char *, size_t);
int		expr_add_element(expr_t *, expr_t *);
void		expr_destroy(expr_t *);

#endif
