/*
 * Copyright (c) 2023 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _QUERY_H_
#define _QUERY_H_

struct query;
typedef struct query query_t;

#ifdef __NXS_PARSER_PRIVATE

/*
 * Lexer (implemented using re2c).
 */

typedef union {
	struct {
		char *	str;
		size_t	len;
	};
	double		fpnum;
} lexval_t;

typedef struct lexer {
	const char *	cursor;
	const char *	marker;
	const char *	token;

	const char *	cur_line;
	unsigned	line;
} lexer_t;

void		lex_init(lexer_t *, const char *);
int		lex(query_t *);

/*
 * Parser (grammar implemented in lemon).
 */

struct query {
	nxs_index_t *	idx;

	/* Lexer and the root expression. */
	lexer_t		lexer;
	lexval_t	lval;
	expr_t *	root;

	/* Syntax error with the message. */
	char *		errmsg;
	bool		error;

	/* Tokenset to be resolved. */
	tokenset_t *	tokens;
};

#endif

query_t *	query_create(nxs_index_t *);
void		query_destroy(query_t *);

int		query_parse(query_t *, const char *);
int		query_prepare(query_t *, unsigned);

void		query_set_error(query_t *);
const char *	query_get_error(query_t *);

#endif
