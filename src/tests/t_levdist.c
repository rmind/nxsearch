/*
 * Unit test: Levenshtein distance.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <err.h>

#include "levdist.h"

static void
levdist_test(const char *s1, const char *s2, int expected)
{
	const size_t s1_len = strlen(s1);
	const size_t s2_len = strlen(s2);
	levdist_t *ctx;
	int d;

	ctx = levdist_create();
	assert(ctx != NULL);

	if ((d = levdist(ctx, s1, s1_len, s2, s2_len)) != expected) {
		errx(EXIT_FAILURE, "%s ~ %s => %u\n", s1, s2, d);
	}
	levdist_destroy(ctx);
}

static void
run_tests(void)
{
	levdist_test("kitten", "kitten", 0);
	levdist_test("kitten", "sitten", 1);
	levdist_test("sitting", "kitten", 3);
	levdist_test("cat", "chat", 1);
	levdist_test("cat", "cactus", 3);
	levdist_test("cat", "gato", 2);

	levdist_test("", "", 0);
	levdist_test("", "a", 1);
	levdist_test("a", "", 1);
	levdist_test("a", "b", 1);

	levdist_test("ab", "ac", 1);
	levdist_test("ac", "bc", 1);
	levdist_test("abc", "axc", 1);
	levdist_test("abc", "def", 3);
	levdist_test("aabbcd", "aabcd", 1);
	levdist_test("aabcd", "aabbcd", 1);
	levdist_test("aaabccc", "", 7);
	levdist_test("ABCDEF", "abcdef", 6);
	levdist_test("ABCDEF", "AbCdEf", 3);

	levdist_test("hello", "hallo", 1);
	levdist_test("variable", "valuable", 2);
	levdist_test("leaf", "leaves", 3);
	levdist_test("ab?cd?ef?", "!ab!cd!ef!", 4);
	levdist_test("john smith", "johnathan smith", 5);
	levdist_test("levenshtein", "frankenstein", 6);
	levdist_test("123456789", "101010101", 8);
	levdist_test("something", "different", 8);
}

int
main(void)
{
	run_tests();
	puts("OK");
	return 0;
}
