/*
 * Unit tests: dtmap index structures.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "nxs.h"
#include "strbuf.h"
#include "index.h"
#include "tokenizer.h"
#include "helpers.h"
#include "utils.h"

static const char *test_tokens1[] = {
	"some-term-1", "another-term-2", "another-term-2"
};

static const char *test_tokens2[] = {
	"term-3"
};

static const uint8_t dtmap_db_exp[] = {
	/*
	 * This serves as a regression test for the ABI breakage.
	 * Verify manually before updating.
	 */
	0x4e, 0x58, 0x53, 0x5f, 0x44, 0x01, 0x00, 0x00, // header ..
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, // data_len = 56
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, // token_count = 4
	0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, // doc_count = 2 | r0
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xe9, // doc_id = 1001
	0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, // doc_len = 3 | n = 2
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, // term_id 1, c = 1
	0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, // term_id 2, c = 2
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xea, // doc_id = 1002
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, // doc_len = 1 | n = 1
	0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, // term_id 3, c = 1
};

static void
prepare_terms(nxs_index_t *idx, tokenset_t *tokens, bool init)
{
	int ret;

	if (init) {
		char *terms_testdb_path = get_tmpfile(NULL);
		ret = idx_terms_open(idx, terms_testdb_path);
		assert(ret == 0);
	}

	ret = idx_terms_add(idx, tokens);
	assert(ret == 0);

	tokenset_resolve(tokens, idx, TOKENSET_STAGE);
	assert(TAILQ_EMPTY(&tokens->staging));
}

static void
check_term_counts(nxs_index_t *idx, nxs_doc_id_t doc1_id, nxs_doc_id_t doc2_id)
{
	idxdoc_t *doc;
	int tc, dc;

	/*
	 * Check the document length and term counts.
	 */
	doc = idxdoc_lookup(idx, doc1_id);
	assert(doc != NULL);

	dc = idxdoc_get_doclen(idx, doc);
	assert(dc == __arraycount(test_tokens1));

	tc = idxdoc_get_termcount(idx, doc, 1);  // some-term-1
	assert(tc == 1);

	tc = idxdoc_get_termcount(idx, doc, 2);  // another-term-2
	assert(tc == 2);

	if (doc2_id) {
		doc = idxdoc_lookup(idx, doc2_id);
		assert(doc != NULL);

		dc = idxdoc_get_doclen(idx, doc);
		assert(dc == __arraycount(test_tokens2));

		tc = idxdoc_get_termcount(idx, doc, 3); // term-3
		assert(tc == 1);
	}
}

static void
run_dtmap_test(void)
{
	char *testdb_path = get_tmpfile(NULL);
	const nxs_doc_id_t doc1_id = 1001, doc2_id = 1002;
	tokenset_t *tokens1, *tokens2;
	nxs_index_t idx;
	int ret;

	memset(&idx, 0, sizeof(idx));
	ret = idx_dtmap_open(&idx, testdb_path);
	assert(ret == 0);

	/*
	 * Add a document 1 with the terms to the index.
	 */

	tokens1 = get_test_tokenset(test_tokens1,
	    __arraycount(test_tokens1), true);
	assert(tokens1 != NULL);
	prepare_terms(&idx, tokens1, true);

	ret = idx_dtmap_add(&idx, doc1_id, tokens1);
	assert(ret == 0);
	tokenset_destroy(tokens1);

	check_term_counts(&idx, doc1_id, 0);

	/*
	 * Add a document 2.
	 */

	tokens2 = get_test_tokenset(test_tokens2,
	    __arraycount(test_tokens2), true);
	assert(tokens2 != NULL);
	prepare_terms(&idx, tokens2, false);

	ret = idx_dtmap_add(&idx, doc2_id, tokens2);
	assert(ret == 0);
	tokenset_destroy(tokens2);

	check_term_counts(&idx, doc1_id, doc2_id);

	ret = idx_dtmap_sync(&idx, 0);
	assert(ret == 0);
	idx_dtmap_close(&idx);

	/* Verify the file contents. */
	ret = mmap_cmp_file(testdb_path, dtmap_db_exp, sizeof(dtmap_db_exp));
	assert(ret == 0);

	/*
	 * Sync using a new index descriptor.
	 */

	ret = idx_dtmap_open(&idx, testdb_path);
	assert(ret == 0);

	ret = idx_dtmap_sync(&idx, 0);
	assert(ret == 0);

	check_term_counts(&idx, doc1_id, doc2_id);

	/*
	 * Check the totals.
	 */
	ret = idx_get_token_count(&idx);
	assert(ret == __arraycount(test_tokens1) + __arraycount(test_tokens2));

	ret = idx_get_doc_count(&idx);
	assert(ret == 2);

	// Cleanup
	idx_dtmap_close(&idx);
	idx_terms_close(&idx);
}

static void
run_dtmap_term_order_test(void)
{
	char *testdb_path = get_tmpfile(NULL);
	tokenset_t *tokens;
	nxs_index_t idx;
	int ret;

	memset(&idx, 0, sizeof(idx));
	ret = idx_dtmap_open(&idx, testdb_path);
	assert(ret == 0);

	/*
	 * Add two documents in different token order.
	 */

	static const char *t1[] = { "a", "m", "c", "x", "n", "z" };
	tokens = get_test_tokenset(t1, __arraycount(t1), true);
	prepare_terms(&idx, tokens, true);
	ret = idx_dtmap_add(&idx, 1001, tokens);
	assert(ret == 0);
	tokenset_destroy(tokens);

	static const char *t2[] = { "z", "m", "x", "c", "n", "a" };
	tokens = get_test_tokenset(t2, __arraycount(t2), false);
	prepare_terms(&idx, tokens, false);
	ret = idx_dtmap_add(&idx, 1002, tokens);
	assert(ret == 0);
	tokenset_destroy(tokens);

	/*
	 * The order in the second document will not be sequential.
	 * Check the term counts.
	 */
	idxdoc_t *doc = idxdoc_lookup(&idx, 1002);
	for (unsigned i = 1; i <= 6; i++) {
		int c;

		c = idxdoc_get_termcount(&idx, doc, i);
		assert(c == 1);
	}

	// Cleanup
	idx_dtmap_close(&idx);
	idx_terms_close(&idx);
}

static void
run_dtmap_partial_sync_test(void)
{
	const char *payload;
	char *basedir = get_tmpdir();
	nxs_index_t *idx, *alt_idx;
	nxs_t *nxs, *alt_nxs;
	int ret;

	nxs = nxs_open(basedir);
	assert(nxs);

	idx = nxs_index_create(nxs, "__test-idx-1", NULL);
	assert(idx);

	/* Add a document. */
	payload = "first second";
	ret = nxs_index_add(idx, NULL, 1001, payload, strlen(payload));
	assert(ret == 0);

	/*
	 * Open another descriptor in parallel and add another document.
	 */
	alt_nxs = nxs_open(basedir);
	assert(alt_nxs);
	alt_idx = nxs_index_open(alt_nxs, "__test-idx-1");
	assert(alt_idx);

	payload = "third";
	ret = nxs_index_add(alt_idx, NULL, 1002, payload, strlen(payload));
	assert(ret == 0);

	nxs_index_close(alt_idx);
	nxs_close(alt_nxs);

	/*
	 * Checks:
	 * - Syncing dtmap without the terms being synced should fail.
	 * - Partial dtmap sync should succeed, but without doc ID 1002.
	 * - Terms and dtmap sync should load doc ID 1002.
	 */

	ret = idx_dtmap_sync(idx, 0);
	assert(ret == -1);

	ret = idx_dtmap_sync(idx, DTMAP_PARTIAL_SYNC);
	assert(ret == 0);
	ASSERT(idxdoc_lookup(idx, 1002) == NULL);

	ret = idx_terms_sync(idx);
	assert(ret == 0);
	ret = idx_dtmap_sync(idx, 0);
	assert(ret == 0);
	ASSERT(idxdoc_lookup(idx, 1002) != NULL);

	// Cleanup
	nxs_index_close(idx);
	nxs_close(nxs);
}

int
main(void)
{
	(void)get_tmpdir();
	run_dtmap_test();
	run_dtmap_term_order_test();
	run_dtmap_partial_sync_test();
	puts("OK");
	return 0;
}
