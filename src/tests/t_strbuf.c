/*
 * Unit test: string buffer primitive.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "strbuf.h"

static void
run_basic_tests(void)
{
	const char s[] = "testing";
	const size_t len = sizeof(s) - 1;
	const size_t llen = STRBUF_DEF_SIZE + 7;
	ssize_t ret;
	char *ls;
	strbuf_t sb;

	memset(&sb, 0xa5, sizeof(strbuf_t));
	strbuf_init(&sb);

	/*
	 * Basic string test.
	 */
	ret = strbuf_acquire(&sb, s, len);
	assert(ret == STRBUF_DEF_SIZE);

	assert(strcmp(sb.value, s) == 0);
	assert(sb.length == len);
	strbuf_release(&sb);

	/*
	 * Size boundary (deduct one for the NIL terminator).
	 */
	ls = calloc(1, STRBUF_DEF_SIZE);
	memset(ls, '.', STRBUF_DEF_SIZE - 1);

	ret = strbuf_acquire(&sb, ls, STRBUF_DEF_SIZE - 1);
	assert(ret == STRBUF_DEF_SIZE);

	assert(strcmp(sb.value, ls) == 0);
	assert(sb.length == STRBUF_DEF_SIZE - 1);
	strbuf_release(&sb);
	free(ls);

	ret = strbuf_prealloc(&sb, STRBUF_DEF_SIZE);
	assert(ret == STRBUF_DEF_SIZE);

	/*
	 * Long string test.
	 */
	ls = calloc(1, llen + 1);
	assert(ls != NULL);
	memset(ls, 'X', llen);

	ret = strbuf_acquire(&sb, ls, llen);
	assert(ret > STRBUF_DEF_SIZE);
	assert(strcmp(sb.value, ls) == 0);
	assert(sb.length == llen);
	free(ls);

	/*
	 * It is permited to call strbuf_acquire() again.
	 * Test with a smaller string this time.
	 */
	ret = strbuf_acquire(&sb, "x", 1);
	assert(ret > STRBUF_DEF_SIZE);  // should not shrink
	assert(strcmp(sb.value, "x") == 0);
	assert(sb.length == 1);
	strbuf_release(&sb);
}

int
main(void)
{
	run_basic_tests();
	puts("OK");
	return 0;
}
