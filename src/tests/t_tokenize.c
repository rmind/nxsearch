/*
 * Unit test: tokenizer API.
 * This code is in the public domain.
 */

#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "nxs.h"
#include "tokenizer.h"
#include "helpers.h"
#include "utils.h"

static const char *test_tokens[] = {
	"some-term-1", "another-term-2", "another-term-2"
};

static const char test_text[] = "The quick brown fox jumped over the lazy dog";

static const char *expected_tokens[] = {
	"the", "quick", "brown", "fox", "jumped", "over", "lazy", "dog"
};

struct test_case_type {
        const char *    text;
        const char *    exp_tokens[];
};

// Standard example. As plain as it gets.
struct test_case_type test_case_1 = {
	"The quick brown fox jumped over the lazy dog.",
	{ "the", "quick", "brown", "fox", "jumped", "over", "lazy", "dog", NULL }
};

// Acronims and emojis.
struct test_case_type test_case_2 = {
	"We will play 🥎 with I.B.M.",
	{ "we", "will", "play", "🥎",  "with", "i.b.m", NULL }
};

// Snake case.
struct test_case_type test_case_3 = {
	"Hello_I_m_arbitrary_concatenated, foo and bar",
	{ "hello_i_m_arbitrary_concatenated", "foo", "and", "bar", NULL }
};

// Markdown and other marking
struct test_case_type test_case_4 = {
	"the [client] is <foo>, some *bold* marks.",
	{ "the", "client", "is", "foo", "some", "bold", "marks", NULL }
};

// Broken spacing
struct test_case_type test_case_5 = {
	"Text,which doesn't  have spaces right;one;two;three..",
	{ "text", "which", "doesn't", "have", "spaces", "right", "one", "two", "three",
	    NULL }
};

// Cases not pasing yet.
struct test_case_type test_case_not_passing = {
	"_underscore_, year-end, join--double Some.Text",
	{ "underscore", "year-end", "join--double", "Some", "text", NULL }
};
struct test_case_type *test_cases[] = {
	&test_case_1,
	&test_case_2,
	&test_case_3,
	&test_case_4,
	&test_case_5,
	// TODO: &test_case_not_passing
};

static void
run_tokenset_test(void)
{
	tokenset_t *tset;
	token_t *token;

	tset = tokenset_create();
	assert(tset != NULL);

	for (unsigned i = 0; i < __arraycount(test_tokens); i++) {
		const char *value = test_tokens[i];
		const size_t len = strlen(value);

		token = token_create(value, len);
		assert(token != NULL);
		tokenset_add(tset, token);
	}

	// Verify the first term.
	token = TAILQ_FIRST(&tset->list);
	assert(token != NULL);
	assert(strcmp(token->buffer.value, "some-term-1") == 0);
	assert(token->count == 1);

	// Verify the second term.
	token = TAILQ_NEXT(token, entry);
	assert(token != NULL);
	assert(strcmp(token->buffer.value, "another-term-2") == 0);
	assert(token->count == 2);

	// There should be no duplicates.
	token = TAILQ_NEXT(token, entry);
	assert(token == NULL);

	tokenset_destroy(tset);
}

static filter_pipeline_t *
get_test_filter_pipeline(nxs_t *nxs, nxs_params_t *params,
    const char **filters, size_t count)
{
	filter_pipeline_t *fp;
	int ret;

	ret = nxs_params_set_str(params, "lang", "en");
	assert(ret == 0);

	ret = nxs_params_set_strlist(params, "filters",
	    filters, __arraycount(filters));

	assert(ret == 0);

	fp = filter_pipeline_create(nxs, params);
	assert(fp != NULL);

	return fp;
}

static void
run_tokenizer_test(void)
{
	char *basedir = get_tmpdir();
	filter_pipeline_t *fp;
	tokenset_t *tset;
	token_t *token;
	nxs_t *nxs;
	nxs_params_t *params = nxs_params_create();
	const char *filters[] = { "normalizer" };
	unsigned i;

	nxs = nxs_open(basedir);
	assert(nxs != NULL);

	fp = get_test_filter_pipeline(nxs, params, filters, __arraycount(filters));
	assert(fp != NULL);

	tset = tokenize(fp, params, test_text, strlen(test_text));
	assert(tset != NULL);

	/*
	 * Verify the tokens.
	 */
	i = 0;
	TAILQ_FOREACH(token, &tset->list, entry) {
		const char *value = token->buffer.value;
		assert(i < __arraycount(expected_tokens));
		assert(strcmp(value, expected_tokens[i]) == 0);
		i++;
	}

	tokenset_destroy(tset);
	filter_pipeline_destroy(fp);
	nxs_close(nxs);
	nxs_params_release(params);
}

static void
run_tokenizer_no_filters_test(void)
{
	char *basedir = get_tmpdir();
	filter_pipeline_t *fp;
	tokenset_t *tset;
	token_t *token;
	nxs_t *nxs;
	nxs_params_t *params = nxs_params_create();
	const char *filters[] = { };
	unsigned i;
	unsigned c;

	nxs = nxs_open(basedir);
	assert(nxs != NULL);

	fp = get_test_filter_pipeline(nxs, params, filters, __arraycount(filters));
	assert(fp != NULL);

	for (c = 0; c < __arraycount(test_cases); c++) {
		tset = tokenize(fp, params, test_cases[c]->text,
		    strlen(test_cases[c]->text));
		assert(tset != NULL);

		const char ** expected = test_cases[c]->exp_tokens;
		i = 0;
		token = TAILQ_FIRST(&tset->list);
		while (expected[i] != NULL) {
			assert(token != NULL);
			const char *value = token->buffer.value;
			assert(strcmp(value, expected[i]) == 0);

			i++;
			token = TAILQ_NEXT(token, entry);
		}
		assert(token == NULL);
		tokenset_destroy(tset);
	}

	filter_pipeline_destroy(fp);
	nxs_close(nxs);
	nxs_params_release(params);
}

int
main(void)
{
	run_tokenset_test();
	run_tokenizer_test();
	run_tokenizer_no_filters_test();
	puts("OK");
	return 0;
}
