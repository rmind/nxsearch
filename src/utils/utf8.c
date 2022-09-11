/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * UTF-8 string handling providing wrappers around the ICU library.
 *
 * u_isalpha() vs u_isUAlphabetic()
 *
 * u_charsToUChars() and u_UCharsToChars()
 *   vs
 * u_strFromUTF8() and u_strToUTF8()
 *
 * u_strtok_r
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <unicode/utypes.h>
#include <unicode/ucasemap.h>
#include <unicode/unorm2.h>

#include "strbuf.h"
#include "utf8.h"
#include "utils.h"

struct utf8_ctx {
	char *			locale;
	UCaseMap *		csm;
	const UNormalizer2 *	normalizer;
	strbuf_t		sbuf;
};

/*
 * utf8_ctx_create: construct a state context for the UTF-8 operations.
 *
 * => Locale is ICU locale ID which is a superset of ISO-639-1.
 */
utf8_ctx_t *
utf8_ctx_create(const char *locale)
{
	utf8_ctx_t *ctx;
	UErrorCode ec;

	ctx = calloc(1, sizeof(utf8_ctx_t));
	if (!ctx) {
		return NULL;
	}
	ctx->locale = locale ? strdup(locale) : NULL;
	strbuf_init(&ctx->sbuf);

	ec = U_ZERO_ERROR;
	ctx->csm = ucasemap_open(locale, U_FOLD_CASE_DEFAULT, &ec);
	if (U_FAILURE(ec)) {
		// const char *errmsg = u_errorName(ec);
		utf8_ctx_destroy(ctx);
		return NULL;
	}

	ec = U_ZERO_ERROR;
	ctx->normalizer = unorm2_getNFKCCasefoldInstance(&ec);
	if (U_FAILURE(ec)) {
		utf8_ctx_destroy(ctx);
		return NULL;
	}

	return ctx;
}

void
utf8_ctx_destroy(utf8_ctx_t *ctx)
{
	if (ctx->csm) {
		ucasemap_close(ctx->csm);
	}
	free(ctx->locale);
	free(ctx);
}

/*
 * utf8_to_utf16: convert UTF-8 string to an UTF-16 array.
 *
 * => The destination array size is in units (16-bit integers).
 * => The destination string will always be NUL-terminated.
 * => Returns the result length in *units* rather than bytes.
 */
ssize_t
utf8_to_utf16(utf8_ctx_t *ctx __unused, const char *u8,
    uint16_t *buf, size_t count)
{
	UErrorCode ec = U_ZERO_ERROR;
	int32_t nunits = 0;

	u_strFromUTF8(buf, count, &nunits, u8, -1 /* NUL-terminated */, &ec);
	if (__predict_false(U_FAILURE(ec) || (size_t)nunits == count)) {
		return -1;
	}
	return nunits;
}

/*
 * utf8_from_utf16: convert UTF-16 array to an UTF-8 string.
 *
 * => The destination string will always be NUL-terminated.
 * => Returns the result length in bytes.
 */
ssize_t
utf8_from_utf16(utf8_ctx_t *ctx __unused, const uint16_t *u16,
    char *buf, size_t buflen)
{
	UErrorCode ec = U_ZERO_ERROR;
	int32_t nbytes = 0;

	u_strToUTF8(buf, buflen, &nbytes, u16, -1 /* NUL-terminated */, &ec);
	if (__predict_false(U_FAILURE(ec) || (size_t)nbytes == buflen)) {
		return -1;
	}
	return nbytes;
}

ssize_t
utf8_tolower(utf8_ctx_t *ctx, const char *s, char *buf, size_t buflen)
{
	UErrorCode ec = U_ZERO_ERROR;
	ssize_t len;

	len = ucasemap_utf8ToLower(ctx->csm, buf, buflen,
	   s, -1 /* NUL-terminated */, &ec);
	if (__predict_false(U_FAILURE(ec) || (size_t)len == buflen)) {
		return -1;
	}
	return len;
}

ssize_t
utf8_toupper(utf8_ctx_t *ctx, const char *s, char *buf, size_t buflen)
{
	UErrorCode ec = U_ZERO_ERROR;
	ssize_t len;

	len = ucasemap_utf8ToUpper(ctx->csm, buf, buflen,
	   s, -1 /* NUL-terminated */, &ec);
	if (__predict_false(U_FAILURE(ec) || (size_t)len == buflen)) {
		return -1;
	}
	return len;
}

/*
 * utf8_normalize: lowercase the UTF-8 string and normalize using
 * Normalization Form KC (NFKC).
 *
 * See: https://www.unicode.org/reports/tr15/
 */
ssize_t
utf8_normalize(utf8_ctx_t *ctx, strbuf_t *buf)
{
	UErrorCode ec = U_ZERO_ERROR;
	uint16_t buf1[128], buf2[128]; // FIXME
	ssize_t len;

	if (utf8_to_utf16(ctx, buf->value, buf1, __arraycount(buf1)) == -1) {
		return -1;
	}
	len = unorm2_normalize(ctx->normalizer,
	    buf1, -1 /* NUL-terminated */, buf2, __arraycount(buf2), &ec);
	if (__predict_false(U_FAILURE(ec) || (size_t)len == __arraycount(buf2))) {
		return -1;
	}
	if (strbuf_prealloc(buf, len + 1) == -1) {
		return -1;
	}
	return utf8_from_utf16(ctx, buf2, buf->value, buf->bufsize);
}
