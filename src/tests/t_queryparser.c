/*
 * Unit test: query parser.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <err.h>

#define __NXSLIB_PRIVATE
#define __NXS_PARSER_PRIVATE
#include "nxs_impl.h"
#include "expr.h"
#include "query.h"
#include "grammar.h"
#include "utils.h"
#include "helpers.h"

typedef struct {
	const char *	query;
	const char *	repr;
	int		tokens[];
} test_case_t;

static const test_case_t test_case_1 = {
	.query = "A",
	.repr = "`A`",
	.tokens = { TOKEN_FF_STRING, 0 },
};

static const test_case_t test_case_2 = {
	.query = "(A OR B) AND C",
	.repr = "(AND (OR `A` `B`) `C`)",
	.tokens = {
		TOKEN_BR_OPEN, TOKEN_FF_STRING, TOKEN_OR, TOKEN_FF_STRING,
		TOKEN_BR_CLOSE, TOKEN_AND, TOKEN_FF_STRING, 0,
	},
};

static const test_case_t test_case_3 = {
	.query = "A OR (B AND C)",
	.repr = "(OR `A` (AND `B` `C`))",
	.tokens = {
		TOKEN_FF_STRING, TOKEN_OR, TOKEN_BR_OPEN, TOKEN_FF_STRING,
		TOKEN_AND, TOKEN_FF_STRING, TOKEN_BR_CLOSE, 0,
	},
};

static const test_case_t test_case_4 = {
	.query = "A OR B AND C",
	.repr = "(OR `A` (AND `B` `C`))",
	.tokens = {
		TOKEN_FF_STRING, TOKEN_OR, TOKEN_FF_STRING, TOKEN_AND,
		TOKEN_FF_STRING, 0,
	},
};

static const test_case_t test_case_5 = {
	.query = "A and not B",
	.repr = "(NOT `A` `B`)",
	.tokens = {
		TOKEN_FF_STRING, TOKEN_AND, TOKEN_NOT, TOKEN_FF_STRING, 0,
	},
};

#if 0
static const test_case_t test_case_6 = {
	.query = "A | B & -C",
	.repr = "(OR `A` (NOT `B` `C`))",
	.tokens = {
		TOKEN_FF_STRING, TOKEN_OR, TOKEN_FF_STRING, TOKEN_AND,
		TOKEN_MINUS, TOKEN_FF_STRING, 0,
	},
};
#endif

static const test_case_t test_case_7 = {
	.query =
	    " \"sp ace\" OR 'quo\\'te' OR ąžuolas OR "
	    "🇬🇧🇺🇸 AND Київ OR (1 AND NOT (  2   OR   3 ))",
	.repr =
	    "(OR (OR (OR (OR `sp ace` `quo\\'te`) `ąžuolas`) "
	    "(AND `🇬🇧🇺🇸` `Київ`)) (NOT `1` (OR `2` `3`)))",
	.tokens = {
		TOKEN_QUOTED_STRING, TOKEN_OR, TOKEN_QUOTED_STRING, TOKEN_OR,
		TOKEN_FF_STRING, TOKEN_OR, TOKEN_FF_STRING, TOKEN_AND,
		TOKEN_FF_STRING, TOKEN_OR, TOKEN_BR_OPEN, TOKEN_FF_STRING,
		TOKEN_AND, TOKEN_NOT, TOKEN_BR_OPEN, TOKEN_FF_STRING,
		TOKEN_OR, TOKEN_FF_STRING, TOKEN_BR_CLOSE, TOKEN_BR_CLOSE, 0,
	},
};

static const test_case_t test_case_8 = {
	.query = "a AND",
	.repr = NULL,  // syntax error
	.tokens = { TOKEN_FF_STRING, TOKEN_AND, 0 },
};

static const test_case_t test_case_9 = {
	.query = "a b OR (c OR d) AND (e",
	.repr = NULL,  // syntax error
	.tokens = {
		TOKEN_FF_STRING, TOKEN_FF_STRING, TOKEN_OR, TOKEN_BR_OPEN,
		TOKEN_FF_STRING, TOKEN_OR, TOKEN_FF_STRING, TOKEN_BR_CLOSE,
		TOKEN_AND, TOKEN_BR_OPEN, TOKEN_FF_STRING, 0
	},
};

static const test_case_t *test_cases[] = {
	&test_case_1, &test_case_2, &test_case_3, &test_case_4, &test_case_5,
	/*&test_case_6,*/ &test_case_7, &test_case_8, &test_case_9,
};

static void
test_query_lexer(const test_case_t *t)
{
	query_t *q;
	unsigned i = 0;
	int token;

	q = query_create(NULL);
	lex_init(&q->lexer, t->query);
	while ((token = lex(q)) > 0) {
		lexval_t *lval = &q->lval;

		if (t->tokens[i] != token) {
			errx(EXIT_FAILURE,
			    "failed query: %s\n"
			    "expected token %u to be %d but got %d",
			    t->query, i, t->tokens[i], token);
		}

		switch (token) {
		case TOKEN_QUOTED_STRING:
		case TOKEN_FF_STRING:
			ASSERT(lval->len);
			free(lval->str);
			break;
		}

		i++;
	}
	query_destroy(q);
}

static char *
expr_string_dump(const expr_t *expr)
{
	static const char *op[] = {
		[EXPR_OP_AND] = "AND",
		[EXPR_OP_OR] = "OR",
		[EXPR_OP_NOT] = "NOT",
	};
	char *buf = NULL;

	if (expr->type == EXPR_VAL_TOKEN) {
		// Use the backtick for strings
		asprintf(&buf, "`%s`", expr->value);
	} else {
		char *e1 = expr_string_dump(expr->elements[0]);
		char *e2 = expr_string_dump(expr->elements[1]);

		asprintf(&buf, "(%s %s %s)", op[expr->type], e1, e2);
		free(e1);
		free(e2);
	}
	return buf;
}

static void
test_query_parser(const test_case_t *t)
{
	query_t *q;
	int ret;

	q = query_create(NULL);
	ret = query_parse(q, t->query);
	assert(ret == 0);

	if (t->repr) {
		char *repr;

		assert(q->root);
		assert(!q->error);

		/* Get the intermediate representation as a string. */
		repr = expr_string_dump(q->root);
		if (strcmp(t->repr, repr)) {
			errx(EXIT_FAILURE,
			    "failed query: %s\n"
			    "exp. representation: %s\n"
			    "seen representation: %s\n",
			    t->query, t->repr, repr);
		}
		free(repr);
	} else {
		/* Expecting syntax error. */
		assert(q->error);
		assert(q->errmsg);
	}
	query_destroy(q);
}

int
main(void)
{
	for (unsigned i = 0; i < __arraycount(test_cases); i++) {
		test_query_lexer(test_cases[i]);
	}
	for (unsigned i = 0; i < __arraycount(test_cases); i++) {
		test_query_parser(test_cases[i]);
	}
	puts("OK");
	return 0;
}
