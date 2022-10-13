/*
 * Unit test: misc.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "index.h"
#include "utils.h"
#include "helpers.h"

static void
run_params_tests(void)
{
	char *testdb_path = get_tmpfile(NULL);
	static const char *test_filters[] = {"a", "b", "c"};
	nxs_params_t *params;
	const char *s, **arr;
	uint64_t val;
	size_t count;
	nxs_t nxs;
	int ret;

	memset(&nxs, 0, sizeof(nxs));
	params = nxs_params_create();
	assert(params);

	/*
	 * Basic types.
	 */

	ret = nxs_params_set_str(params, "lang", "en");
	assert(ret == 0);

	ret = nxs_params_set_uint(params, "n", 0xdeadbeef);
	assert(ret == 0);

	ret = nxs_params_set_strset(params, "filters",
	    test_filters, __arraycount(test_filters));
	assert(ret == 0);

	/*
	 * Serialize-unserialize test.
	 */

	ret = nxs_params_serialize(&nxs, params, testdb_path);
	assert(ret == 0);
	nxs_params_release(params);

	params = nxs_params_unserialize(&nxs, testdb_path);
	assert(params);

	s = nxs_params_get_str(params, "lang");
	assert(strcmp(s, "en") == 0);

	ret = nxs_params_get_uint(params, "n", &val);
	assert(ret == 0 && val == 0xdeadbeef);

	arr = nxs_params_get_strset(params, "filters", &count);
	assert(arr && count == 3);
	assert(strcmp(arr[0], "a") == 0);
	assert(strcmp(arr[1], "b") == 0);
	assert(strcmp(arr[2], "c") == 0);
	free(arr);

	nxs_params_release(params);
}

static void
run_resp_tests(void)
{
	nxs_resp_t *resp;
	char *s;
	int ret;

	resp = nxs_resp_create(1000);
	assert(resp);

	ret = nxs_resp_addresult(resp, &(const idxdoc_t){ .id = 1 }, 1.5);
	assert(ret == 0);

	ret = nxs_resp_addresult(resp, &(const idxdoc_t){ .id = 2 }, 3);
	assert(ret == 0);

	nxs_resp_build(resp);

	s = nxs_resp_tojson(resp, NULL);
	nxs_resp_release(resp);

	assert(strcmp(s,
	    "{\"results\":[{\"doc_id\":2,\"score\":3.0},"
	    "{\"doc_id\":1,\"score\":1.5}],\"count\":2}") == 0);
	free(s);
}

int
main(void)
{
	run_params_tests();
	run_resp_tests();
	puts("OK");
	return 0;
}
