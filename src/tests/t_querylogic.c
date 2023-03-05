/*
 * Unit test: query logic.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "nxs.h"
#include "index.h"
#include "helpers.h"
#include "utils.h"

static const test_doc_t docs[] = {
	{ 1, "Textbook about Erlang in Linux environment" },
	{ 2, "Unix Shell scripting textbook" },
	{ 3, "Erlang and Python examples" },
	{ 4, "Textbook about Python using Linux and Windows" },
	{ 5, "All but NOT: Textbook Erlang Python Shell Linux Unix Java" },
	{ 6, "All keywords: Textbook Erlang Python Shell Linux Unix" },
};

#if 0
static const test_search_case_t test_case_1 = {
	.docs = docs, .doc_count = __arraycount(docs),
	.query = "textbook AND (Erlang OR Python OR Shell) AND "
	         "(Linux OR Unix) AND NOT (Windows OR Java)",
	.scores = {
		DOC_ID_ONLY(1),
		DOC_ID_ONLY(2),
		DOC_ID_ONLY(6),
		END_TEST_SCORE
	}
};
#endif

static const test_search_case_t test_case_2 = {
	.docs = docs, .doc_count = __arraycount(docs),
	.query = "unix",
	.scores = {
		DOC_ID_ONLY(2),
		DOC_ID_ONLY(5),
		DOC_ID_ONLY(6),
		END_TEST_SCORE
	}
};

static const test_search_case_t *test_cases[] = {
	/* &test_case_1, */ &test_case_2
};

int
main(void)
{
	for (unsigned i = 0; i < __arraycount(test_cases); i++) {
		test_index_search(test_cases[i]);
	}
	puts("OK");
	return 0;
}
