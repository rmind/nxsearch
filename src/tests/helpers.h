/*
 * Unit test helpers.
 * This code is in the public domain.
 */

#ifndef _TESTS_HELPERS_H_
#define _TESTS_HELPERS_H_

#include "tokenizer.h"
#include "index.h"

typedef void (*test_func_t)(nxs_index_t *);

typedef struct {
	nxs_doc_id_t	id;
	const char *	text;
} test_doc_t;

typedef struct {
	nxs_doc_id_t	id;
	// Two value slots for: TF-IDF and BM25
	float		value[2];
} test_score_t;

#define	END_TEST_SCORE	{ 0, { 0, 0 } }

typedef struct {
	const test_doc_t *docs;
	unsigned	doc_count;
	const char *	query;
	test_score_t	scores[];
} test_score_case_t;

char *		get_tmpdir(void);
char *		get_tmpfile(const char *);
int		mmap_cmp_file(const char *, const unsigned char *, size_t);

tokenset_t *	get_test_tokenset(const char *[], size_t);

void		run_with_index(const char *, const char *, bool, test_func_t);

void		test_index_search(const test_score_case_t *);

#endif
