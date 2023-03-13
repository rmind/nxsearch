/*
 * Copyright (c) 2023 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

%include {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define __NXSLIB_PRIVATE
#define __NXS_PARSER_PRIVATE
#include "nxs_impl.h"
#include "expr.h"
#include "query.h"
#include "utils.h"
}

%extra_context { query_t *q }

/*
 * The grammar does not use the special "error" rule, therefore syntax_error
 * handler is the first and the only point of error handling.  There can be
 * multiple invocations, though, as parser pops the stack.
 */
%syntax_error {
	if (!q->error) {
		query_set_error(q);
	}
}

%parse_failure {
	ASSERT(q->error);
	ASSERT(q->root == NULL);
}

/*
 * Zero stack size will make the parser use dynamic memory (this is an
 * undocumented feature of lemon).
 */
%stack_size 0
%stack_overflow {
	q->error = true;
}

/*
 * lexval_t has passed-by-value semantics; parser may copy it and save
 * it in its stack.  Strings are consumed and therefore must be destroyed
 * together with the expression (or in a destructor if the values gets
 * popped before it's reduced to an expr).
 */
%token_prefix TOKEN_
%token_type { lexval_t }

%type expr { expr_t * }
%type expr_list { expr_t * }
%type value { char * }

%destructor expr_list { expr_destroy($$); }
%destructor expr { expr_destroy($$); }
%destructor value { free($$); }

// Use the precedence in logics (¬, ∧, ∨).
%left OR.
%left AND.
%left NOT.

query ::= expr_list(E).
{
	q->root = E;
}

expr_list(EL) ::= expr(E).
{
	EL = E;
}

expr_list(E) ::= expr_list(L) expr(R).
{
	E = expr_create_operator(EXPR_OP_OR, L, R);
}

expr(E) ::= expr(L) AND expr(R).
{
	E = expr_create_operator(EXPR_OP_AND, L, R);
}

expr(E) ::= expr(L) OR expr(R).
{
	E = expr_create_operator(EXPR_OP_OR, L, R);
}

expr(E) ::= expr(L) AND NOT expr(R).
{
	E = expr_create_operator(EXPR_OP_NOT, L, R);
}

expr(E) ::= BR_OPEN expr(BE) BR_CLOSE.
{
	E = BE;
}

expr(E) ::= value(V).
{
	// Note: the string value will be consumed rather than copied.
	E = expr_create_token(V);
}

value(V) ::= FF_STRING(T).
{
	V = T.str;
}

value(V) ::= QUOTED_STRING(T).
{
	V = T.str;
}

%code {

int
query_parse(query_t *q, const char *query)
{
	yyParser parser;
	int token;

	lex_init(&q->lexer, query);
	ParseInit(&parser, q);

	while ((token = lex(q)) > 0) {
		/* Note: we copy lexval_t. */
		Parse(&parser, token, q->lval);
	}
	Parse(&parser, 0, q->lval);
	ParseFinalize(&parser);
	return 0;
}

}
