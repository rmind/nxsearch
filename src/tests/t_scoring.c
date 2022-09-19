/*
 * Unit test: ranking/scoring logic.
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

static const test_doc_t docs_1[] = {
	{ 1, "The quick brown fox jumped over the lazy dog" },
	{ 2, "Once upon a time there were three little foxes" },
};

#define	DOG_TFIDF_SCORE		1.1736
#define	FOX_TFIDF_SCORE		0.693147

#define	DOG_BM25_SCORE		0.253785
#define	FOX_BM25_SCORE		0.066754

/*
 * IDF: Terms occurring in fewer documents should give higher score.
 * In other words, rarer terms are better.
 */

static_assert(DOG_TFIDF_SCORE > FOX_TFIDF_SCORE);
static_assert(DOG_BM25_SCORE > FOX_BM25_SCORE);

static const test_score_case_t test_case_1 = {
	/*
	 * Basic search: verify the expected score.
	 */
	.docs = docs_1, .doc_count = __arraycount(docs_1),
	.query = "dog", .scores = {
		{ 1, { DOG_TFIDF_SCORE, DOG_BM25_SCORE } },
		END_TEST_SCORE
	}
};

static const test_score_case_t test_case_2 = {
	/*
	 * Equal scores for each document.
	 */
	.docs = docs_1, .doc_count = __arraycount(docs_1),
	.query = "fox", .scores = {
		{ 1, { FOX_TFIDF_SCORE, FOX_BM25_SCORE } },
		{ 2, { FOX_TFIDF_SCORE, FOX_BM25_SCORE } },
		END_TEST_SCORE
	}
};

static const test_score_case_t test_case_3 = {
	/*
	 * Scores for each term should be summed.
	 */
	.docs = docs_1, .doc_count = __arraycount(docs_1),
	.query = "fox dog", .scores = {
		{ 1,
			{
				DOG_TFIDF_SCORE + FOX_TFIDF_SCORE,
				DOG_BM25_SCORE + FOX_BM25_SCORE,
			}
		},
		{ 2, { FOX_TFIDF_SCORE, FOX_BM25_SCORE } },
		END_TEST_SCORE
	}
};

static const test_doc_t docs_2[] = {
	{ 1, "cat dog rat" },
	{ 2, "cat cat dog" },
};

static const test_score_case_t test_case_4 = {
	/*
	 * TF: Documents matching more terms should have higher score.
	 */
	.docs = docs_2, .doc_count = __arraycount(docs_2),
	.query = "cat", .scores = {
		{ 1, { 0.693147, 0.066754 } },
		{ 2, { 1.098612, 0.087140 } },
		END_TEST_SCORE
	}
};

static const test_doc_t docs_3[] = {
	{ 1, "cat cat dog dog" },
	{ 2, "dog dog cat cat" },
	{ 3, "cat dog rat cow" },
	{ 4, "cat dog rat bat" },
};

static const test_score_case_t test_case_5 = {
	/*
	 * Documents matching more different terms (variety) should
	 * have higher score.
	 */
	.docs = docs_3, .doc_count = __arraycount(docs_3),
	.query = "cat dog rat cow", .scores = {
		{ 1, { 2.197225, 0.100713 } },
		{ 2, { 2.197225, 0.100713 } },
		{ 3, { 4.213948, 0.771754 } },
		{ 4, { 2.559895, 0.330938 } },
		END_TEST_SCORE
	}
};

static const test_doc_t docs_4[] = {
	{ 1, "aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa" },
	{ 2, "aa aa aa aa aa aa aa aa aa aa bb bb bb bb bb bb bb bb bb bb" },
	{ 3, "aa bb bb bb bb bb bb bb bb bb bb bb bb bb bb bb bb bb bb bb" },
};

static const test_score_case_t test_case_6 = {
	/*
	 * TF bound for term saturation (BM25): many matches of the same
	 * term in the given document should have a bound.  BM25 is expected
	 * to demonstrate a much smoother slope than TF-IDF (especially from
	 * DOC 2 to DOC 1).
	 */
	.docs = docs_4, .doc_count = __arraycount(docs_4),
	.query = "aa", .scores = {
		{ 1, { 3.044523, 0.095780 } },
		{ 2, { 2.397895, 0.088995 } },
		{ 3, { 0.693147, 0.048890 } },
		END_TEST_SCORE
	}
};

static const test_doc_t docs_5[] = {
	{
		1, // 3x cats, long doc
		"This is a very long document about the cats "
		"All kind of cats including the tabby and other cats"
	},
	{ 2, "cats cats cats" },	// 3x cats, short doc
	{ 3, "cats cats dogs" },	// only 2x cats, same length
};

static const test_score_case_t test_case_7 = {
	/*
	 * Document length (BM25): matches in shorter documents
	 * should give higher score.  No difference for TF-IDF.
	 */
	.docs = docs_5, .doc_count = __arraycount(docs_5),
	.query = "cats", .scores = {
		{ 1, { 1.386294, 0.048411 } },
		{ 2, { 1.386294, 0.091469 } },
		{ 3, { 1.098612, 0.084499 } },
		END_TEST_SCORE
	}
};

static const test_score_case_t *test_cases[] = {
	&test_case_1, &test_case_2, &test_case_3, &test_case_4,
	&test_case_5, &test_case_6, &test_case_7
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
