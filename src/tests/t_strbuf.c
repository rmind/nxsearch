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
	char *ls;
	strbuf_t sb;

	memset(&sb, 0xa5, sizeof(strbuf_t));
	strbuf_init(&sb);

	/*
	 * Basic string test.
	 */
	strbuf_acquire(&sb, s, len);
	assert(strcmp(sb.value, s) == 0);
	assert(sb.length == len);
	strbuf_release(&sb);

	/*
	 * Long string test.
	 */

	ls = malloc(llen + 1);
	assert(ls != NULL);
	memset(ls, 'X', llen);
	ls[llen] = '\0';

	strbuf_acquire(&sb, ls, llen);
	assert(strcmp(sb.value, ls) == 0);
	assert(sb.length == llen);
	strbuf_release(&sb);
	free(ls);
}

int
main(void)
{
	run_basic_tests();
	puts("OK");
	return 0;
}
