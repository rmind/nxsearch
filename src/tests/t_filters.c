/*
 * Unit tests: filters.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "filters.h"
#include "helpers.h"
#include "utils.h"

static filter_action_t
run_filter_test(nxs_t *nxs, nxs_params_t *params,
    const char *token, const char *exp_value)
{
	filter_pipeline_t *fp;
	filter_action_t action;
	strbuf_t sbuf;

	strbuf_init(&sbuf);
	strbuf_acquire(&sbuf, token, strlen(token));

	fp = filter_pipeline_create(nxs, params);
	assert(fp);

	action = filter_pipeline_run(fp, &sbuf);
	assert(!exp_value || strcmp(sbuf.value, exp_value) == 0);

	filter_pipeline_destroy(fp);
	strbuf_release(&sbuf);

	return action;
}

static filter_action_t
test_filter(void *arg __unused, strbuf_t *buf)
{
	/*
	 * Test filter where the token value decides the action.
	 */
	if (strcmp(buf->value, "M") == 0) {
		return FILT_MUTATION;
	}
	if (strcmp(buf->value, "D") == 0) {
		return FILT_DISCARD;
	}
	return FILT_ERROR;
}

static void
run_filter_action_tests(void)
{
	static const char *filters[] = { "test-filter" };
	static const filter_ops_t test_filter_ops = {
		.filter = test_filter,
	};
	char *basedir = get_tmpdir();
	filter_action_t action;
	nxs_params_t *params;
	nxs_t *nxs;
	int ret;

	nxs = nxs_open(basedir);
	assert(nxs != NULL);

	/*
	 * Setup the test filter.
	 */

	ret = nxs_filter_register(nxs, "test-filter", &test_filter_ops, NULL);
	assert(ret == 0);

	// Duplicate filter registration check
	ret = nxs_filter_register(nxs, "test-filter", &test_filter_ops, NULL);
	assert(ret == -1 && nxs_get_error(nxs, NULL) == NXS_ERR_EXISTS);

	params = nxs_params_create();
	assert(params);

	ret = nxs_params_set_strlist(params, "filters",
	    filters, __arraycount(filters));
	assert(ret == 0);

	/*
	 * Test all actions.
	 */

	action = run_filter_test(nxs, params, "M", "M");
	assert(action == FILT_MUTATION);

	action = run_filter_test(nxs, params, "D", "D");
	assert(action == FILT_DISCARD);

	action = run_filter_test(nxs, params, "E", "E");
	assert(action == FILT_ERROR);

	nxs_params_release(params);
	nxs_close(nxs);
}

static void
run_lua_test(const char *code)
{
	static const char *filters[] = { "lua-test-filter" };
	char *basedir = get_tmpdir();
	nxs_params_t *params;
	filter_action_t action;
	nxs_t *nxs;
	int ret;

	assert(code != NULL);
	nxs = nxs_open(basedir);
	assert(nxs != NULL);

	ret = nxs_luafilter_load(nxs, "lua-test-filter", code);
	assert(ret == 0);

	params = nxs_params_create();
	assert(params);

	ret = nxs_params_set_str(params, "lang", "en");
	assert(ret == 0);

	ret = nxs_params_set_strlist(params, "filters",
	    filters, __arraycount(filters));
	assert(ret == 0);

	/*
	 * The filter should lower-case the string.
	 */
	action = run_filter_test(nxs, params, "TEST-STRING", "test-string");
	assert(action == FILT_MUTATION);

	nxs_params_release(params);
	nxs_close(nxs);
}

static void
run_lua_tests(void)
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
	run_filter_action_tests();
	run_lua_tests();
	puts("OK");
	return 0;
}
