/*
 * Unit test: document removal from the index.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "nxs.h"
#include "index.h"
#include "helpers.h"
#include "utils.h"

static void
add_docs(nxs_index_t *idx)
{
	const char *text = "abc def ghi";
	int ret;

	for (unsigned i = 1; i <= 3; i++) {
		ret = nxs_index_add(idx, i, text, strlen(text));
		assert(ret == 0);
	}
}

static void
verify_docs(nxs_index_t *idx)
{
	idxdoc_t *doc;

	doc = idxdoc_lookup(idx, 1);
	ASSERT(doc != NULL);

	ASSERT(idxdoc_lookup(idx, 2) == NULL);

	doc = idxdoc_lookup(idx, 3);
	ASSERT(doc != NULL);

	ASSERT(idx_get_doc_count(idx) == 2);
	ASSERT(idx_get_token_count(idx) == 3 * 2);
}

static void
run_removal_test(void)
{
	char *basedir = get_tmpdir();
	nxs_index_t *idx, *alt_idx;
	nxs_t *nxs, *alt_nxs;
	int ret;

	nxs = nxs_create(basedir);
	assert(nxs);

	idx = nxs_index_create(nxs, "test-idx", NULL);
	assert(idx);

	/*
	 * Add three documents.
	 */
	add_docs(idx);
	ASSERT(idxdoc_lookup(idx, 2));
	ASSERT(idx_get_doc_count(idx) == 3);
	ASSERT(idx_get_token_count(idx) == 3 * 3);

	/*
	 * Open another active descriptor in parallel.
	 */
	alt_nxs = nxs_create(basedir);
	assert(alt_nxs);
	alt_idx = nxs_index_open(alt_nxs, "test-idx");
	assert(alt_idx);
	ASSERT(idxdoc_lookup(alt_idx, 2));

	/*
	 * Remove the middle document and verify.
	 */
	ret = nxs_index_remove(idx, 2);
	assert(ret == 0);
	verify_docs(idx);
	nxs_index_close(idx);

	/*
	 * Sync the other descriptor and verify there.
	 */
	ret = idx_dtmap_sync(alt_idx);
	assert(ret == 0);
	verify_docs(alt_idx);

	nxs_index_close(alt_idx);
	nxs_destroy(alt_nxs);

	/*
	 * Verify using the fresh descriptor.
	 */
	idx = nxs_index_open(nxs, "test-idx");
	assert(idx);

	assert(idx_terms_sync(idx) == 0 && idx_dtmap_sync(idx) == 0);
	verify_docs(idx);

	nxs_index_close(idx);
	nxs_destroy(nxs);
}

int
main(void)
{
	run_removal_test();
	puts("OK");
	return 0;
}
