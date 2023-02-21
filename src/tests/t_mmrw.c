/*
 * Unit test: memory read-write abstraction.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

#include "mmrw.h"
#include "utils.h"

static void
run_basic_tests(void)
{
	const char *test_str = "testing";
	char inbuf[8], buf[8];
	mmrw_t mm;

	/*
	 * Check: advance and fetch out of bounds.
	 */

	mmrw_init(&mm, inbuf, 1);

	assert(mmrw_advance(&mm, 1) == 1);
	assert(mmrw_advance(&mm, 1) == -1);
	assert(mmrw_fetch(&mm, buf, 1) == -1);

	/*
	 * Fetch one, but not more.
	 * The destination buffer must be untouched on failure.
	 */

	inbuf[0] = 0x5a;
	mmrw_init(&mm, inbuf, 1);
	assert(mmrw_fetch(&mm, buf, 1) == 1 && buf[0] == 0x5a);

	buf[0] = 0x1;
	assert(mmrw_fetch(&mm, buf, 1) == -1 && buf[0] == 0x1);

	/*
	 * Basic store-fetch.
	 */

	mmrw_init(&mm, inbuf, sizeof(inbuf));
	assert(mmrw_store(&mm, test_str, sizeof(test_str)) == 8);

	mmrw_init(&mm, inbuf, sizeof(inbuf));
	assert(mmrw_fetch(&mm, buf, 8) == 8);
	assert(strcmp(buf, test_str) == 0);
}

static void
run_integer_tests(void)
{
	unsigned char buf[2 + 4 + 8];
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;
	mmrw_t mm;

	mmrw_init(&mm, buf, sizeof(buf));
	assert(mmrw_store16(&mm, 0x4008) == 2);
	assert(mmrw_store32(&mm, 0x10000010U) == 4);
	assert(mmrw_store64(&mm, 0x200040010004018UL) == 8);

	mmrw_init(&mm, buf, sizeof(buf));
	assert(mmrw_fetch16(&mm, &u16) == 2 && u16 == 0x4008);
	assert(mmrw_fetch32(&mm, &u32) == 4 && u32 == 0x10000010U);
	assert(mmrw_fetch64(&mm, &u64) == 8 && u64 == 0x200040010004018UL);
}

static void
run_seek_tests(void)
{
	char v, buf[] = { 0, 1, 2, 3, 4, 5 };
	size_t offset;
	mmrw_t mm;

	mmrw_init(&mm, buf, sizeof(buf));

	assert(mmrw_advance(&mm, 3) == 3);
	offset = MMRW_GET_OFFSET(&mm);

	assert(mmrw_fetch(&mm, &v, sizeof(v)) == 1);
	assert(v == 3);

	assert(mmrw_advance(&mm, 1) == 1);
	assert(mmrw_fetch(&mm, &v, sizeof(v)) == 1);
	assert(v == 5);

	assert(mmrw_seek(&mm, offset) == (ssize_t)offset);
	assert(mmrw_fetch(&mm, &v, sizeof(v)) == 1);
	assert(v == 3);
}

int
main(void)
{
	run_basic_tests();
	run_integer_tests();
	run_seek_tests();
	puts("OK");
	return 0;
}
