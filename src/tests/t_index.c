/*
 * Unit tests: some general index tests.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "helpers.h"
#include "utils.h"

#define	TEST_IDX	"__test-idx-0"

static void
run_index_checks(void)
{
	char *basedir = get_tmpdir();
	char *idxpath = NULL;
	nxs_index_t *idx;
	nxs_t *nxs;
	int ret;

	// Non-existent base directory
	nxs = nxs_open("/tmp/__nxsearch/non-existing-directory");
	assert(!nxs && errno == ENOENT);

	nxs = nxs_open(basedir);
	assert(nxs);

	// Non-existent index
	idx = nxs_index_open(nxs, TEST_IDX "-non-existent");
	assert(!idx);

	// Already exists
	idx = nxs_index_create(nxs, TEST_IDX, NULL);
	assert(idx);

	idx = nxs_index_create(nxs, TEST_IDX, NULL);
	assert(!idx && nxs_get_error(nxs, NULL) == NXS_ERR_EXISTS);

	asprintf(&idxpath, "%s/data/%s/", basedir, TEST_IDX);
	assert(fs_is_dir(idxpath));

	// Index destruction
	ret = nxs_index_destroy(nxs, TEST_IDX);
	assert(ret == 0 && !fs_is_dir(idxpath));

	nxs_close(nxs);
	free(idxpath);
}

static void
run_index_name_checks(void)
{
	const char *invalid_names[] = { "a/b", "..", ".", "/" };
	char *basedir = get_tmpdir();
	nxs_t *nxs;

	nxs = nxs_open(basedir);
	assert(nxs);

	for (unsigned i = 0; i < __arraycount(invalid_names); i++) {
		const char *name = invalid_names[i];
		nxs_index_t *idx;
		nxs_err_t err;
		int ret;

		idx = nxs_index_create(nxs, name, NULL);
		assert(!idx && nxs_get_error(nxs, NULL) == NXS_ERR_INVALID);

		ret = nxs_index_destroy(nxs, name);
		err = nxs_get_error(nxs, NULL);
		assert(ret == -1 && err == NXS_ERR_INVALID);
	}
	nxs_close(nxs);
}

static void
run_index_request_checks(void)
{
	char *basedir = get_tmpdir();
	nxs_index_t *idx;
	nxs_resp_t *resp;
	nxs_t *nxs;
	int ret;

	nxs = nxs_open(basedir);
	assert(nxs);

	idx = nxs_index_create(nxs, TEST_IDX, NULL);
	assert(idx);

	// Zero document ID
	ret = nxs_index_add(idx, NULL, 0, "x", 1);
	assert(ret == -1 && nxs_get_error(nxs, NULL) == NXS_ERR_INVALID);

	// Document ID already exists
	ret = nxs_index_add(idx, NULL, 1001, "x", 1);
	assert(ret == 0);

	ret = nxs_index_add(idx, NULL, 1001, "x", 1);
	assert(ret == -1 && nxs_get_error(nxs, NULL) == NXS_ERR_EXISTS);

	// Empty text
	ret = nxs_index_add(idx, NULL, 1002, "", 0);
	assert(ret == -1 && nxs_get_error(nxs, NULL) == NXS_ERR_MISSING);

	// Empty search (syntax error)
	resp = nxs_index_search(idx, NULL, "", 0);
	assert(!resp && nxs_get_error(nxs, NULL) == NXS_ERR_INVALID);

	// Cleanup
	nxs_index_close(idx);
	nxs_close(nxs);
}

static void
run_index_race_check(void)
{
	char *testdb_path = get_tmpfile(NULL);
	idxmap_t idxmap;
	bool created;
	int ret;

	ret = creat(testdb_path, 0664);
	close(ret);

	memset(&idxmap, 0, sizeof(idxmap_t));
	ret = idx_db_open(&idxmap, testdb_path, &created);
	assert(ret == -1 && errno == EIO);
}

int
main(void)
{
	run_index_checks();
	run_index_name_checks();
	run_index_request_checks();
	run_index_race_check();
	puts("OK");
	return 0;
}
