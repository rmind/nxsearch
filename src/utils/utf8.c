/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * UTF-8 string handling providing wrappers around the ICU library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <unicode/utypes.h>
#include <unicode/ucasemap.h>
#include <unicode/unorm2.h>
#include <unicode/utrans.h>

#include "strbuf.h"
#include "utf8.h"
#include "utils.h"

/*
* UCI Rule for transliteration.
* see more: https://unicode-org.github.io/icu/userguide/transforms/general/
*/
static const char NFKD_RULE[] = "NFKD; [:Nonspacing Mark:] Remove; Latin-ASCII; NFKC";


struct utf8_ctx {
	char *			locale;
	UCaseMap *		csm;
	const UNormalizer2 *	normalizer;
	UTransliterator *	transliterator;
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
	UChar *translit_id;

	ctx = calloc(1, sizeof(utf8_ctx_t));
	if (!ctx) {
		return NULL;
	}
	ctx->locale = locale ? strdup(locale) : NULL;

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

	ec = U_ZERO_ERROR;
	translit_id = malloc(sizeof(NFKD_RULE) * sizeof(UChar));
	u_strFromUTF8(translit_id, sizeof(NFKD_RULE), NULL, NFKD_RULE, -1, &ec);
	if (U_FAILURE(ec)) {
		free(translit_id);
		utf8_ctx_destroy(ctx);
		return NULL;
	}
	ec = U_ZERO_ERROR;
	ctx->transliterator = utrans_openU(translit_id, -1, UTRANS_FORWARD, NULL, 0, NULL, &ec);
	free(translit_id);
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
	if (ctx->transliterator){
		utrans_close(ctx->transliterator);
	}
	free(ctx->locale);
	free(ctx);
}

/*
 * utf8_to_utf16: convert UTF-8 string to an UTF-16 array.
 *
 * => The destination array size is in units (16-bit integers).
 * => The destination string will always be NUL-terminated.
 * => Returns the result length in *units* rather than bytes (excl NUL-term).
 */
ssize_t
utf8_to_utf16(utf8_ctx_t *ctx __unused, const char *u8,
    uint16_t *buf, size_t count)
{
	UErrorCode ec = U_ZERO_ERROR;
	int32_t nunits = 0;

	u_strFromUTF8(buf, count, &nunits, u8, -1 /* NUL-terminated */, &ec);
	if (__predict_false(U_FAILURE(ec) || (size_t)nunits >= count)) {
		return -1;
	}
	return nunits;
}

/*
 * utf8_from_utf16: convert UTF-16 array to an UTF-8 string.
 *
 * => The destination string will always be NUL-terminated.
 * => Returns the result length in bytes (excl NUL-term).
 */
ssize_t
utf8_from_utf16(utf8_ctx_t *ctx __unused, const uint16_t *u16,
    char *buf, size_t buflen)
{
	UErrorCode ec = U_ZERO_ERROR;
	int32_t nbytes = 0;

	u_strToUTF8(buf, buflen, &nbytes, u16, -1 /* NUL-terminated */, &ec);
	if (__predict_false(U_FAILURE(ec) || (size_t)nbytes >= buflen)) {
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
	if (__predict_false(U_FAILURE(ec) || (size_t)len >= buflen)) {
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
	if (__predict_false(U_FAILURE(ec) || (size_t)len >= buflen)) {
		return -1;
	}
	return len;
}


/*
 * utf8_subs_diacritics: uses standard ICU transformation to remove 
 * diacritic characters.
 * 
 * See more: https://unicode-org.github.io/icu/userguide/transforms/general/
 */
ssize_t
utf8_subs_diacritics(utf8_ctx_t *ctx, strbuf_t *buf)
{
	UErrorCode ec = U_ZERO_ERROR;
	uint16_t *ubuf = NULL;
	size_t ulen;
	int32_t len, max_len;

	/*
	 * Convert from UTF-8 to UTF-16: the buffer should be twice as
	 * large, keeping in mind an extra unit for the NUL terminator, also the
	 * transformation can produce more characters, therefore give
	 * a large buffer size (XXX: 3x is an arbitrary choice).
	 */
	ulen = roundup2((buf->length + 1) * 3, 64);
	if ((ubuf = malloc(ulen * 2)) == NULL) {
		goto out;
	}
	if ((len = utf8_to_utf16(ctx, buf->value, ubuf, ulen)) == -1) {
		goto out;
	}
	max_len = len;

	utrans_transUChars(
		ctx->transliterator, ubuf, &len, 
		ulen, 0, &max_len, &ec
	);

	if (__predict_false(U_FAILURE(ec))) {
		const char *errmsg __unused = u_errorName(ec);
		app_dbgx("utrans_transUChars() failed: %s", errmsg);
		goto out;
	}

	if (strbuf_prealloc(buf, max_len) == -1) {
		goto out;
	}
	len = utf8_from_utf16(ctx, ubuf, buf->value, buf->bufsize);
	if (len == -1) {
		goto out;
	}
	buf->length = len;
out:
	/* Always ensure the string is NUL terminated. */
	buf->value[buf->length] = '\0';
	free(ubuf);
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
	uint16_t *src_ubuf = NULL, *norm_ubuf = NULL;
	size_t src_ulen, norm_ulen, max_len;
	ssize_t c, len = -1;

	/*
	 * Convert from UTF-8 to UTF-16: the buffer should be twice as
	 * large, keeping in mind an extra unit for the NUL terminator.
	 */
	src_ulen = (buf->length + 1) * 2;
	if ((src_ubuf = malloc(src_ulen)) == NULL) {
		goto out;
	}
	if ((c = utf8_to_utf16(ctx, buf->value, src_ubuf, src_ulen)) == -1) {
		goto out;
	}

	/*
	 * Normalization may produce more characters, therefore give
	 * a large buffer size (XXX: 3x is an arbitrary choice).
	 */
	norm_ulen = roundup2((c + 1) * 3, 64);
	if ((norm_ubuf = malloc(norm_ulen * 2)) == NULL) {
		goto out;
	}
	c = unorm2_normalize(ctx->normalizer, src_ubuf, -1,
	    norm_ubuf, norm_ulen, &ec);
	if (__predict_false(U_FAILURE(ec) || (size_t)c >= norm_ulen)) {
		const char *errmsg __unused = u_errorName(ec);
		app_dbgx("unorm2_normalize() failed: %s", errmsg);
		goto out;
	}

	/*
	 * Convert back from UTF-16 to UTF-8.  Ensure the sufficient
	 * buffer size and set the new string length.  Note that the
	 * UTF-8 might produce more bytes than UTF-16 units, therfore
	 * again give some extra space.
	 */
	max_len = (c + 1) * 2;  // XXX: x2 is arbitary
	if (strbuf_prealloc(buf, max_len) == -1) {
		goto out;
	}
	len = utf8_from_utf16(ctx, norm_ubuf, buf->value, buf->bufsize);
	if (len == -1) {
		goto out;
	}
	buf->length = len;
out:
	/* Always ensure the string is NUL terminated. */
	buf->value[buf->length] = '\0';
	free(src_ubuf);
	free(norm_ubuf);
	return len;
}
