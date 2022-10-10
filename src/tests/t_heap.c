/*
 * Unit test: decomposed heap sort.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#include "heap.h"
#include "utils.h"

typedef struct {
	unsigned value;
} obj_t;

typedef struct {
	unsigned	n;
	obj_t *		nums;
	unsigned *	exp_nums;
} test_case_t;

static int
cmp_uint(const void *p1, const void *p2)
{
	const unsigned v1 = *(const unsigned *)p1;
	const unsigned v2 = *(const unsigned *)p2;

	if (v1 < v2)
		return -1;
	if (v1 > v2)
		return 1;
	return 0;
}

static int
cmp_obj(const void *obj1, const void *obj2)
{
	const obj_t *o1 = obj1;
	const obj_t *o2 = obj2;
	return cmp_uint(&o1->value, &o2->value);
}

static test_case_t *
get_random_numbers(unsigned n)
{
	test_case_t *t = calloc(1, sizeof(test_case_t));

	t->n = n;
	t->nums = calloc(n, sizeof(obj_t));
	t->exp_nums = calloc(n, sizeof(unsigned));

	for (unsigned i = 0; i < n; i++) {
		unsigned v = random() % 100;
		t->nums[i].value = v;
		t->exp_nums[i] = v;
	}
	qsort(t->exp_nums, n, sizeof(unsigned), cmp_uint);
	return t;
}

static void
cleanup_testcase(test_case_t *t)
{
	free(t->nums);
	free(t->exp_nums);
	free(t);
}

static void
run_minheap_test(test_case_t *t, unsigned cap)
{
	heap_t *h;

	h = heap_create(cap, cmp_obj);
	assert(h);

	for (unsigned i = 0; i < t->n; i++) {
		bool ret = heap_add(h, &t->nums[i]);
		assert(ret);
	}
	for (unsigned i = 0; i < t->n; i++) {
		const obj_t *num = heap_remove_min(h);
		assert(num->value == t->exp_nums[i]);
	}
	heap_destroy(h);
}

static void
run_sort_test(test_case_t *t, unsigned cap)
{
	heap_t *h;
	obj_t **items;
	size_t c;

	h = heap_create(cap, cmp_obj);
	assert(h);

	for (unsigned i = 0; i < t->n; i++) {
		bool ret = heap_add(h, &t->nums[i]);
		assert(ret);
	}

	items = heap_sort(h, &c);
	assert(c == t->n);

	for (unsigned i = 0; i < c; i++) {
		const unsigned j = c - i - 1;  // reverse
		assert(items[i]->value == t->exp_nums[j]);
	}
	heap_destroy(h);
}

static void
run_tests(void)
{
	unsigned q = 100;
	test_case_t *t;

	srandom(1);

	for (unsigned i = 1; i <= q; i++) {
		t = get_random_numbers(i);
		run_minheap_test(t, q);
		run_sort_test(t, q);
		cleanup_testcase(t);
	}
}

int
main(void)
{
	run_tests();
	puts("OK");
	return 0;
}
