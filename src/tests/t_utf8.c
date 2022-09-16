/*
 * Unit test: UTF-8 string primitives.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <err.h>

#include "strbuf.h"
#include "utf8.h"
#include "utils.h"

static void
run_snowman_test(void)
{
	const char *snowman = "☃";  // unicode snowman
	const char snowman_u8[] = { 0xE2, 0x98, 0x83 };  // UTF-8 encoding
	const uint16_t snowman_u16[] = { 0x2603 };  // UTF-16 encoding
	utf8_ctx_t *ctx;
	uint16_t u16buf[2];
	char u8buf[4];
	ssize_t count;

	ctx = utf8_ctx_create(NULL);

	/* Fill the buffers as we will test for NUL-terminator. */
	memset(u8buf, 0xff, sizeof(u8buf));
	memset(u16buf, 0xff, sizeof(u16buf));

	/*
	 * Convert from UTF-8 to UTF-16 and check the encoding.
	 */
	count = utf8_to_utf16(ctx, snowman, u16buf, __arraycount(u16buf));
	assert(count == 1);
	assert(memcmp(u16buf, snowman_u16, sizeof(snowman_u16)) == 0);
	assert(u16buf[1] == 0x0);  // must be NUL-terminated

	count = utf8_from_utf16(ctx, u16buf, u8buf, __arraycount(u8buf));
	assert(count == 3);
	assert(memcmp(u8buf, snowman_u8, sizeof(snowman_u8)) == 0);
	assert(u8buf[3] == 0x0);  // must be NUL-terminated

	/*
	 * Additionally, attempt to convert using too small buffer.
	 * Must fail because there is no space for NUL-terminator.
	 */
	memset(u8buf, 0xff, sizeof(u8buf));
	count = utf8_from_utf16(ctx, u16buf, u8buf, 3);
	assert(count == -1);

	count = utf8_to_utf16(ctx, snowman, u16buf, 1);
	assert(count == -1);

	utf8_ctx_destroy(ctx);
}

static void
run_case_test(void)
{
	utf8_ctx_t *ctx;
	char buf[64];
	int ret;

	ctx = utf8_ctx_create(NULL);

	ret = utf8_tolower(ctx, "TEST", buf, sizeof(buf));
	assert(ret && strcmp(buf, "test") == 0);

	ret = utf8_tolower(ctx, "ĄČĘĖĮŠŲŪŽ", buf, sizeof(buf));
	assert(ret && strcmp(buf, "ąčęėįšųūž") == 0);

	ret = utf8_toupper(ctx, "straße", buf, sizeof(buf));
	assert(ret && strcmp(buf, "STRASSE") == 0);

	ret = utf8_toupper(ctx, "Дніпр", buf, sizeof(buf));
	assert(ret && strcmp(buf, "ДНІПР") == 0);

	utf8_ctx_destroy(ctx);
}

static void
run_norm_test(void)
{
	struct {
		const char *	input;
		const char *	expected;
	} test_cases[] = {
		// General normalization
		{ "Henry Ⅷ", "henry viii" },
		{ "AirForce ①", "airforce 1" },
		{"５０３４４４０", "5034440"}
#if 0
		// Removal of diacritics
		{ "ĄŽUOLĖLIS", "azuolelis" },
		{ "Fuglafjørður", "fuglafjordur" },
		{ "Árbæ", "arbae" },
#endif
	};
	utf8_ctx_t *ctx;
	strbuf_t buf;

	ctx = utf8_ctx_create(NULL);
	strbuf_init(&buf);

	for (unsigned i = 0; i < __arraycount(test_cases); i++) {
		const char *input = test_cases[i].input;
		const char *expected = test_cases[i].expected;
		int ret;

		ret = strbuf_acquire(&buf, input, strlen(input));
		assert(ret > 0);

		ret = utf8_normalize(ctx, &buf);
		assert(ret && strcmp(buf.value, expected) == 0);
	}

	strbuf_release(&buf);
	utf8_ctx_destroy(ctx);
}

int
main(void)
{
	run_snowman_test();
	run_case_test();
	run_norm_test();
	puts("OK");
	return 0;
}
