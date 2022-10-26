/*
 * Unit tests: BK-tree.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <string.h>

#include "deque.h"
#include "bktree.h"
#include "levdist.h"
#include "utils.h"

static int
bktree_levdist(void *ctx, const void *a, const void *b)
{
	const char *val_1 = a, *val_2 = b;
	return levdist(ctx, val_1, strlen(val_1), val_2, strlen(val_2));
}

static void
run_tests(void)
{
	const char *test_words[] = {
		"the", "quick", "brown", "fox", "jumped", "over", "lazy", "dog"
	};
	const char *search_words[] = {
		"teh", "qvick", "brawn", "fox", "jumps", "ovr", "llazy", "dog"
	};
	bktree_t *bkt;
	levdist_t *levctx;
	deque_t *dq;
	int ret;

	levctx = levdist_create();
	assert(levctx);

	bkt = bktree_create(bktree_levdist, levctx);
	assert(bkt);

	for (unsigned i = 0; i < __arraycount(test_words); i++) {
		ret = bktree_insert(bkt, test_words[i]);
		assert(ret == 0);
	}

	dq = deque_create(64);
	assert(dq);

	for (unsigned i = 0; i < __arraycount(test_words); i++) {
		char *result;

		ret = bktree_search(bkt, 2, search_words[i], dq);
		assert(ret == 0);

		result = deque_pop_back(dq);
		assert(result && strcmp(result, test_words[i]) == 0);
	}
	deque_destroy(dq);

	bktree_destroy(bkt);
	levdist_destroy(levctx);
}

int
main(void)
{
	run_tests();
	puts("OK");
	return 0;
}
