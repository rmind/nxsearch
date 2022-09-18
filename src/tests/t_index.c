/*
 * Unit test: indexing and search.
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

static const struct {
	const char *	text;
	nxs_doc_id_t	id;
} test_docs[] = {
	{
		"The quick brown fox jumped over the lazy dog",
		1
	},
	{
		"Once upon a time there were three little foxes",
		2
	},
};

static void
run_general(void)
{
	char *basedir = get_tmpdir();
	const char *query;
	nxs_results_t *results;
	nxs_index_t *idx;
	nxs_t *nxs;

	nxs = nxs_create(basedir);
	assert(nxs);

	idx = nxs_index_create(nxs, "test-idx");
	assert(idx);

	for (unsigned i = 0; i < __arraycount(test_docs); i++) {
		const nxs_doc_id_t doc_id = test_docs[i].id;
		const char *text = test_docs[i].text;
		int ret;

		ret = nxs_index_add(idx, doc_id, text, strlen(text));
		assert(ret == 0);
	}

	/*
	 * Basic search.  Check scores as a basic regression test.
	 */

	query = "dog";
	results = nxs_index_search(idx, query, strlen(query));

	assert(results->count == 1);
	check_doc_score(results, 1, 1.1736);

	nxs_results_release(results);

	query = "fox";
	results = nxs_index_search(idx, query, strlen(query));

	assert(results->count == 2);
	check_doc_score(results, 1, 0.693147);
	check_doc_score(results, 2, 0.693147);

	nxs_results_release(results);

	nxs_index_close(nxs, idx);
	nxs_destroy(nxs);
}

int
main(void)
{
	run_general();
	puts("OK");
	return 0;
}
