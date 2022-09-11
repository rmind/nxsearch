/*
 * Unit test: terms index structures.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "strbuf.h"
#include "index.h"
#include "tokenizer.h"
#include "helpers.h"
#include "utils.h"

static const char *test_tokens[] = {
	"some-term-1", "another-term-2", "another-term-2"
};

static const uint8_t terms_db_exp[] = {
	/*
	 * This serves as a regression test for the ABI breakage.
	 * Verify manually before updating.
	 */
	0x4e, 0x58, 0x53, 0x5f, 0x54, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x2f, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x0b, 0x73, 0x6f, 0x6d, 0x65, 0x2d, 0x74,
	0x65, 0x72, 0x6d, 0x2d, 0x31, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0e,
	0x61, 0x6e, 0x6f, 0x74, 0x68, 0x65, 0x72, 0x2d,
	0x74, 0x65, 0x72, 0x6d, 0x2d, 0x32, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
};

static void
check_terms(fts_index_t *idx)
{
	static const char *expected_tokens[] = {
		"some-term-1", "another-term-2"
	};
	for (unsigned i = 0; i < __arraycount(expected_tokens); i++) {
		const char *token = expected_tokens[i];
		const size_t len = strlen(token);
		const idxterm_t *term = idxterm_lookup(idx, token, len);

		assert(term != NULL);
		assert(term->id == i + 1);
		assert(strcmp(term->value, token) == 0);
		assert(term->offset > 0);
	}
}

static void
run_idxterm_test(void)
{
	char *testdb_path = get_tmpfile(NULL);
	const char tval[] = "test-1";
	idxterm_t *term, *tp;
	fts_index_t idx;
	int ret;

	memset(&idx, 0, sizeof(idx));
	ret = idxterm_sysinit(&idx);
	assert(ret == 0);

	ret = idx_terms_open(&idx, testdb_path);
	assert(ret == 0);

	tp = idxterm_create(&idx, tval, sizeof(tval) - 1, 1001);
	assert(tp != NULL);

	term = idxterm_lookup(&idx, tval, sizeof(tval) - 1);
	assert(term == tp && term->offset == 1001);

	idxterm_destroy(&idx, term);

	idx_terms_close(&idx);
	idxterm_sysfini(&idx);
}

static void
run_terms_test(void)
{
	char *testdb_path = get_tmpfile(NULL);
	fts_index_t idx;
	tokenset_t *tset;
	int ret;

	memset(&idx, 0, sizeof(idx));
	ret = idxterm_sysinit(&idx);

	/*
	 * Add some terms to the index.
	 */
	ret = idx_terms_open(&idx, testdb_path);
	assert(ret == 0);

	tset = get_test_tokenset(test_tokens, __arraycount(test_tokens));
	assert(tset != NULL);

	ret = idx_terms_add(&idx, tset);
	assert(ret == 0);
	tokenset_destroy(tset);

	/* Check that the terms are also in-memory. */
	check_terms(&idx);

	/* Verify the file contents. */
	ret = mmap_cmp_file(testdb_path, terms_db_exp, sizeof(terms_db_exp));
	assert(ret == 0);

	/*
	 * Sync using the same index descriptor.
	 */
	ret = idx_terms_sync(&idx);
	assert(ret == 0);
	idx_terms_close(&idx);

	/*
	 * Sync using a new index descriptor.
	 */
	ret = idx_terms_open(&idx, testdb_path);
	assert(ret == 0);

	ret = idx_terms_sync(&idx);
	assert(ret == 0);

	check_terms(&idx);

	idx_terms_close(&idx);
	idxterm_sysfini(&idx);
}

int
main(void)
{
	(void)get_tmpdir();
	run_idxterm_test();
	run_terms_test();
	puts("OK");
	return 0;
}
