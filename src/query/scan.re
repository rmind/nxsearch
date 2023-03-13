/*
 * Copyright (c) 2023 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define __NXSLIB_PRIVATE
#define __NXS_PARSER_PRIVATE
#include "nxs_impl.h"
#include "expr.h"
#include "query.h"
#include "grammar.h"

#define	TOKEN_EOF	(0)

void
lex_init(lexer_t *ctx, const char *s)
{
	ctx->cursor = s;
	ctx->cur_line = s;
	ctx->line = 1;
}

static inline void
lex_nextline(lexer_t *ctx)
{
	ctx->cur_line = ctx->token;
	ctx->line++;
}

static size_t
lex_get_token_len(lexer_t *ctx)
{
	return (uintptr_t)ctx->cursor - (uintptr_t)ctx->token;
}

int
lex(query_t *q)
{
	lexer_t *ctx = &q->lexer;
	lexval_t *lval = &q->lval;
loop:
	ctx->token = ctx->cursor;

	/*!re2c

	re2c:define:YYCTYPE = "unsigned char";
	re2c:yyfill:enable = 0;

	re2c:define:YYCURSOR = ctx->cursor;
	re2c:define:YYMARKER = ctx->marker;

	END		= "\x00";
	EOL		= "\n";
	SP		= [ \t\v\f\r\n];
	WSP		= SP+;

	AND		= '&' | 'AND';
	OR		= '|' | 'OR';
	NOT		= 'NOT';

	//
	// Quoted string and free-form string (anything but separators).
	//

	SQ_STR		= ['] ([^\x00'\\] | [\\][^\x00])* ['];
	DQ_STR		= ["] ([^\x00"\\] | [\\][^\x00])* ["];
	STR		= SQ_STR | DQ_STR;

	FF_STR		= ([^\x00] \ SP \ "(" \ ")")+;

	//
	// Terminators
	//

	*		{ query_set_error(q); return -1; }
	END		{ return TOKEN_EOF; }

	//
	// End of line and whitespaces
	//

	EOL		{ lex_nextline(ctx); goto loop; }
	WSP		{ goto loop; }

	//
	// Operators
	//

	AND		{ return TOKEN_AND; }
	OR		{ return TOKEN_OR; }
	NOT		{ return TOKEN_NOT; }
	"("		{ return TOKEN_BR_OPEN; }
	")"		{ return TOKEN_BR_CLOSE; }

	//
	// Strings
	//

	STR
	{
		lval->len = lex_get_token_len(ctx);
		lval->str = strndup(ctx->token + 1, lval->len - 2);
		return TOKEN_QUOTED_STRING;
	}

	FF_STR
	{
		lval->len = lex_get_token_len(ctx);
		lval->str = strndup(ctx->token, lval->len);
		return TOKEN_FF_STRING;
	}

	*/
}
