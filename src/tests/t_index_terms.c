/*
 * Unit tests: terms index structures.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "nxs.h"
#include "strbuf.h"
#include "index.h"
#include "storage.h"
#include "tokenizer.h"
#include "helpers.h"
#include "utils.h"

static const char *test_tokens[] = {
	"some-term-1", "another-term-2", "another-term-2"
};

static const uint8_t terms_db_exp[] = {
	/*
	 * This serves as a regression test for the ABI breakage.
	 * WARNING: Verify manually before updating.
	 */
	0x4e, 0x58, 0x53, 0x5f, 0x54, 0x01, 0x00, 0x00, // header ..
	0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, // data_len 47 | r0
	0x00, 0x0b, 0x73, 0x6f, 0x6d, 0x65, 0x2d, 0x74, // len 11, some-term-t1
	0x65, 0x72, 0x6d, 0x2d, 0x31, 0x00, 0x00, 0x00, // .. nil | pad
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // tc = 1
	0x00, 0x0e, 0x61, 0x6e, 0x6f, 0x74, 0x68, 0x65, // len = 14, ..
	0x72, 0x2d, 0x74, 0x65, 0x72, 0x6d, 0x2d, 0x32, // another-term-2
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // .. nil | pad
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, // tc = 2
};

static void
check_terms(nxs_index_t *idx)
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
	nxs_index_t idx;
	int ret;

	memset(&idx, 0, sizeof(idx));
	ret = idx_terms_open(&idx, testdb_path);
	assert(ret == 0);

	tp = idxterm_create(tval, sizeof(tval) - 1, 1001);
	assert(tp != NULL);

	term = idxterm_insert(&idx, tp, 1);
	assert(term == tp);

	term = idxterm_lookup(&idx, tval, sizeof(tval) - 1);
	assert(term == tp && term->offset == 1001);

	idxterm_destroy(&idx, term);

	idx_terms_close(&idx);
}

static void
run_terms_test(void)
{
	char *testdb_path = get_tmpfile(NULL);
	tokenset_t *tokens;
	nxs_index_t idx;
	int ret;

	memset(&idx, 0, sizeof(idx));

	/*
	 * Add some terms to the index.
	 */
	ret = idx_terms_open(&idx, testdb_path);
	assert(ret == 0);

	tokens = get_test_tokenset(test_tokens,
	    __arraycount(test_tokens), true);
	assert(tokens != NULL);

	ret = idx_terms_add(&idx, tokens);
	assert(ret == 0);
	tokenset_destroy(tokens);

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
}

static void
run_terms_dup_test(void)
{
	char *testdb_path = get_tmpfile(NULL);
	tokenset_t *tokens;
	nxs_index_t idx;
	int ret;

	memset(&idx, 0, sizeof(idx));

	ret = idx_terms_open(&idx, testdb_path);
	assert(ret == 0);

	// Add terms
	tokens = get_test_tokenset(test_tokens,
	    __arraycount(test_tokens), true);
	assert(tokens != NULL);

	ret = idx_terms_add(&idx, tokens);
	assert(ret == 0);
	tokenset_destroy(tokens);

	// Add the same terms again
	tokens = get_test_tokenset(test_tokens,
	    __arraycount(test_tokens), true);
	assert(tokens != NULL);

	ret = idx_terms_add(&idx, tokens);
	assert(ret == 0);
	tokenset_destroy(tokens);

	// Verify the file contents, it should be the same.
	ret = mmap_cmp_file(testdb_path, terms_db_exp, sizeof(terms_db_exp));
	assert(ret == 0);
	idx_terms_close(&idx);
}

static int
run_terms_verify_header(nxs_t *nxs, const void *header, size_t len)
{
	char *testdb_path = get_tmpfile(NULL);
	nxs_index_t idx;
	FILE *fp;
	int ret;

	fp = fopen(testdb_path, "w");
	assert(fp);

	ret = fwrite(header, len, 1, fp);
	assert(ret == 1);
	fclose(fp);

	memset(&idx, 0, sizeof(idx));
	idx.nxs = nxs;
	return idx_terms_open(&idx, testdb_path);
}

static void
run_terms_verify_test(void)
{
	char *basedir = get_tmpdir();
	void *header;
	nxs_t *nxs;
	int ret;

	header = calloc(1, IDX_SIZE_STEP);
	assert(header);

	nxs = nxs_open(basedir);
	assert(nxs);

	// Incomplete header
	ret = run_terms_verify_header(nxs, header, 1);
	assert(ret == -1 && nxs_get_error(nxs, NULL) == NXS_ERR_SYSTEM);

	// Invalid mark (just zeros in the header)
	ret = run_terms_verify_header(nxs, header, IDX_SIZE_STEP);
	assert(ret == -1 && nxs_get_error(nxs, NULL) == NXS_ERR_FATAL);

	// Valid mark, but invalid ABI version
	memcpy(header, NXS_T_MARK, sizeof(NXS_T_MARK) - 1);
	ret = run_terms_verify_header(nxs, header, IDX_SIZE_STEP);
	assert(ret == -1 && nxs_get_error(nxs, NULL) == NXS_ERR_FATAL);

	nxs_close(nxs);
	free(header);
}

int
main(void)
{
	(void)get_tmpdir();
	run_idxterm_test();
	run_terms_test();
	run_terms_dup_test();
	run_terms_verify_test();
	puts("OK");
	return 0;
}
