/*
 * Unit test: dtmap index structures.
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
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, // total_tokens = 4
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

	idxterm_resolve_tokens(idx, tokens, true);
	assert(TAILQ_EMPTY(&tokens->staging));
}

static void
check_term_counts(nxs_index_t *idx, nxs_doc_id_t doc1_id, nxs_doc_id_t doc2_id)
{
	idxdoc_t *doc;
	int tc;

	doc = idxdoc_lookup(idx, doc1_id);
	assert(doc != NULL);

	tc = idxdoc_get_termcount(idx, doc, 1);  // some-term-1
	assert(tc == 1);

	tc = idxdoc_get_termcount(idx, doc, 2);  // another-term-2
	assert(tc == 2);

	if (doc2_id) {
		doc = idxdoc_lookup(idx, doc2_id);
		assert(doc != NULL);
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
	ret = idxterm_sysinit(&idx);

	ret = idx_dtmap_open(&idx, testdb_path);
	assert(ret == 0);

	/*
	 * Add a document 1 with the terms to the index.
	 */
	tokens1 = get_test_tokenset(test_tokens1, __arraycount(test_tokens1));
	assert(tokens1 != NULL);
	prepare_terms(&idx, tokens1, true);

	ret = idx_dtmap_add(&idx, doc1_id, tokens1);
	assert(ret == 0);
	tokenset_destroy(tokens1);

	check_term_counts(&idx, doc1_id, 0);

	/*
	 * Add a document 2.
	 */
	tokens2 = get_test_tokenset(test_tokens2, __arraycount(test_tokens2));
	assert(tokens2 != NULL);
	prepare_terms(&idx, tokens2, false);

	ret = idx_dtmap_add(&idx, doc2_id, tokens2);
	assert(ret == 0);
	tokenset_destroy(tokens2);

	check_term_counts(&idx, doc1_id, doc2_id);

	ret = idx_dtmap_sync(&idx);
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

	ret = idx_dtmap_sync(&idx);
	assert(ret == 0);

	check_term_counts(&idx, doc1_id, doc2_id);

	// Cleanup
	idx_dtmap_close(&idx);
	idx_terms_close(&idx);
	idxterm_sysfini(&idx);
}

int
main(void)
{
	(void)get_tmpdir();
	run_dtmap_test();
	puts("OK");
	return 0;
}
