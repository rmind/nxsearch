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

static void
run_params_tests(void)
{
	nxs_params_t *params;
	char *s;
	int ret;

	params = nxs_params_create();
	assert(params);

	ret = nxs_params_set_str(params, "lang", "en");
	assert(ret == 0);

	ret = nxs_params_set_uint(params, "n", 1);
	assert(ret == 0);

	s = nxs_params_tojson(params, NULL);
	nxs_params_release(params);

	assert(strcmp(s, "{\"lang\":\"en\",\"n\":1}") == 0);
	free(s);
}

static void
run_resp_tests(void)
{
	nxs_resp_t *resp;
	char *s;
	int ret;

	resp = nxs_resp_create();
	assert(resp);

	ret = nxs_resp_addresult(resp, &(const idxdoc_t){ .id = 1 }, 1.5);
	assert(ret == 0);
	nxs_resp_build(resp);

	s = nxs_resp_tojson(resp, NULL);
	nxs_resp_release(resp);

	assert(strcmp(s,
	    "{\"results\":[{\"doc_id\":1,\"score\":1.5}],\"count\":1}") == 0);
	free(s);
}

int
main(void)
{
	run_params_tests();
	run_resp_tests();
	return 0;
}
