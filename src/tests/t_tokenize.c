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
get_test_filter_pipeline(nxs_t *nxs)
{
	const char *filters[] = { "normalizer" };
	filter_pipeline_t *fp;

	fp = filter_pipeline_create(nxs, "en", filters, __arraycount(filters));
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
	unsigned i;

	nxs = nxs_create(basedir);
	assert(nxs != NULL);

	fp = get_test_filter_pipeline(nxs);
	assert(fp != NULL);

	tset = tokenize(fp, test_text, strlen(test_text));
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
	nxs_destroy(nxs);
}

int
main(void)
{
	run_tokenset_test();
	run_tokenizer_test();
	puts("OK");
	return 0;
}
