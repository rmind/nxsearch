/*
 * Unit tests: deque.
 * This code is in the public domain.
 */

#include <stdio.h>

#include "deque.h"
#include "utils.h"

static void
run_tests(void)
{
	deque_t * dq;
	uintptr_t pval;
	int ret;

	dq = deque_create(4, 4);
	assert(dq);

	for (unsigned i = 1; i <= 4; i++) {
		ret = deque_push(dq, (void *)(uintptr_t)i);
		assert(ret == 0);
	}

	/*
	 * Pop front and push to the back: triggers a wraparound.
	 */
	pval = (uintptr_t)deque_pop_front(dq);
	assert(pval == 1);

	ret = deque_push(dq, (void *)(uintptr_t)5);
	assert(ret == 0);

	/* Check that the next pop value is right. */
	pval = (uintptr_t)deque_pop_front(dq);
	assert(pval == 2);

	/*
	 * Push two values: triggers array resize.
	 */
	ret = deque_push(dq, (void *)(uintptr_t)6);
	assert(ret == 0);

	ret = deque_push(dq, (void *)(uintptr_t)7);
	assert(ret == 0);

	/*
	 * Check that the front and the back are still correct.
	 */
	pval = (uintptr_t)deque_pop_front(dq);
	assert(pval == 3);

	pval = (uintptr_t)deque_pop_back(dq);
	assert(pval == 7);

	pval = (uintptr_t)deque_pop_front(dq);
	assert(pval == 4);

	/*
	 * At this point two slots [2..3] are populated.
	 * Fill the rest i.e. all 8 slots.
	 */
	for (unsigned i = 7; i <= 12; i++) {
		ret = deque_push(dq, (void *)(uintptr_t)i);
		assert(ret == 0);
	}

	/* Pop all backwards and verify. */
	for (unsigned i = 12; i >= 5; i--) {
		pval = (uintptr_t)deque_pop_back(dq);
		assert(pval == i);
	}

	/* The queue is empty. */
	assert(deque_pop_front(dq) == NULL);
	assert(deque_pop_back(dq) == NULL);

	deque_destroy(dq);
}

int
main(void)
{
	run_tests();
	puts("OK");
	return 0;
}
