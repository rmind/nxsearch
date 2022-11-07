/*
 * Unit tests: filters.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nxs.h"
#include "filters.h"
#include "helpers.h"
#include "utils.h"

static void
run_filter_test(nxs_t *nxs, nxs_params_t *params)
{
	const char *s = "TEST-STRING";
	filter_pipeline_t *fp;
	filter_action_t action;
	strbuf_t sbuf;

	strbuf_init(&sbuf);
	strbuf_acquire(&sbuf, s, strlen(s));

	fp = filter_pipeline_create(nxs, params);
	assert(fp);

	action = filter_pipeline_run(fp, &sbuf);
	assert(action == FILT_MUTATION);

	/* The filter should lower-case the string. */
	assert(strcmp(sbuf.value, "test-string") == 0);

	filter_pipeline_destroy(fp);
	strbuf_release(&sbuf);
}

static void
run_lua_test(const char *code)
{
	static const char *filters[] = { "test-filter" };
	char *basedir = get_tmpdir();
	nxs_params_t *params;
	nxs_t *nxs;
	int ret;

	assert(code != NULL);
	nxs = nxs_open(basedir);
	assert(nxs != NULL);

	ret = nxs_luafilter_load(nxs, "test-filter", code);
	assert(ret == 0);

	params = nxs_params_create();
	assert(params);

	ret = nxs_params_set_str(params, "lang", "en");
	assert(ret == 0);

	ret = nxs_params_set_strlist(params, "filters",
	    filters, __arraycount(filters));
	assert(ret == 0);

	run_filter_test(nxs, params);

	nxs_params_release(params);
	nxs_close(nxs);
}

static void
run_tests(void)
{
	char *code;

	run_lua_test(
	    "return {"
	    "  filter = function(ctx, val) return string.lower(val) end"
	    "}"
	);

	code = fs_read_file("./tests/test_filter.lua", NULL);
	run_lua_test(code);
	free(code);
}

int
main(void)
{
	run_tests();
	puts("OK");
	return 0;
}
