/*
 * Copyright (c) 2022-2023 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Tokenizer
 *
 * - Processes the text splitting it into tokens separated by whitespace.
 * - Invokes the filter pipeline to process each token.
 * - Constructs a list of processed tokens, associated with terms.
 */

#include <stdlib.h>
#include <string.h>
#include <unicode/ustring.h>
#include <unicode/ubrk.h>
#include <unicode/utypes.h>

#define __NXSLIB_PRIVATE
#include "tokenizer.h"
#include "index.h"
#include "utils.h"
#include "utf8.h"
#include "nxs_impl.h"

/*
 * token_create: create a token by creating a copy of the given token.
 */
token_t *
token_create(const char *value, size_t len)
{
	token_t *token;

	if ((token = malloc(sizeof(token_t))) == NULL) {
		return NULL;
	}
	strbuf_init(&token->buffer);

	if (strbuf_acquire(&token->buffer, value, len) == -1) {
		free(token);
		return NULL;
	}
	token->idxterm = NULL;
	token->count = 0;
	return token;
}

void
token_destroy(token_t *token)
{
	strbuf_release(&token->buffer);
	free(token);
}

tokenset_t *
tokenset_create(void)
{
	tokenset_t *tset;

	if ((tset = calloc(1, sizeof(tokenset_t))) == NULL) {
		return NULL;
	}
	TAILQ_INIT(&tset->list);
	TAILQ_INIT(&tset->staging);
	tset->map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	return tset;
}

void
tokenset_destroy(tokenset_t *tset)
{
	token_t *token;

	TAILQ_CONCAT(&tset->list, &tset->staging, entry);
	while ((token = TAILQ_FIRST(&tset->list)) != NULL) {
		TAILQ_REMOVE(&tset->list, token, entry);
		token_destroy(token);
	}
	ASSERT(TAILQ_EMPTY(&tset->staging));
	rhashmap_destroy(tset->map);
	free(tset);
}

/*
 * tokenset_add: add the token to the set or increment the counter on
 * how many times this token was seen if it is already in the set.
 *
 * => Returns the current token, if there is one in the list already.
 * => Otherwise, returns the value of the 'token' parameter.
 */
token_t *
tokenset_add(tokenset_t *tset, token_t *token)
{
	const strbuf_t *str = &token->buffer;
	token_t *current_token;

	current_token = rhashmap_get(tset->map, str->value, str->length);
	if (current_token) {
		/* Already in the set: increment the counter. */
		current_token->count++;
		token_destroy(token);
		tset->seen++;
		return current_token;
	}

	token->count = 1;
	TAILQ_INSERT_TAIL(&tset->list, token, entry);
	rhashmap_put(tset->map, str->value, str->length, token);

	tset->data_len += str->length;
	tset->count++;
	tset->seen++;
	return token;
}

static void
tokenset_remove(tokenset_t *tset, token_t *token)
{
	const strbuf_t *str = &token->buffer;

	rhashmap_del(tset->map, str->value, str->length);
	TAILQ_REMOVE(&tset->list, token, entry);

	tset->data_len -= str->length;
	tset->seen -= token->count;
	tset->count--;

	token_destroy(token);
}

/*
 * tokenset_moveback: move the staged token back to the list.
 */
void
tokenset_moveback(tokenset_t *tset, token_t *token)
{
	TAILQ_REMOVE(&tset->staging, token, entry);
	TAILQ_INSERT_TAIL(&tset->list, token, entry);
	ASSERT(tset->staged > 0);
	tset->staged--;
}

/*
 * tokenset_resolve: lookup the in-memory term object for each token.
 * If found, then associate it with the token.
 *
 * => If the TOKENSET_FUZZYMATCH flag is set and no term is found for
 *    the given token, then attempt to fuzzy search for a matching term.
 *
 * => If the TOKENSET_STAGE flag is set and no term is found, then move
 *    the given token to a separate staging list.
 *
 * => If the TOKENSET_TRIM flag is set and no term is found, then remove
 *    the token from the list and destroy it.  This flag and TOKENSET_STAGE
 *    are mutually exclusive.
 */
void
tokenset_resolve(tokenset_t *tset, nxs_index_t *idx, unsigned flags)
{
	const bool stage = (flags & TOKENSET_STAGE) != 0;
	const bool fuzzymatch = (flags & TOKENSET_FUZZYMATCH) != 0;
	const bool trim = (flags & TOKENSET_TRIM) != 0;
	token_t *token;

	ASSERT((stage | trim) == 0 || (stage ^ trim) != 0);

	token = TAILQ_FIRST(&tset->list);
	while (token) {
		token_t *next_token = TAILQ_NEXT(token, entry);
		const char *value = token->buffer.value;
		const size_t len = token->buffer.length;
		idxterm_t *term;

		term = idxterm_lookup(idx, value, len);
		if (!term && fuzzymatch) {
			term = idxterm_fuzzysearch(idx, value, len);
		}
		if (!term) {
			if (stage) {
				TAILQ_REMOVE(&tset->list, token, entry);
				TAILQ_INSERT_TAIL(&tset->staging, token, entry);
				tset->staged++;
				app_dbgx("staging %p [%s]", token, value);
			}
			if (trim) {
				app_dbgx("removing %p [%s]", token, value);
				tokenset_remove(tset, token);
				token = NULL;
			}
		} else {
			app_dbgx("[%s] => %u", value, term->id);
			token->idxterm = term;
		}
		token = next_token;
	}
}

/*
 * tokenize_value: create a token for the given value, run the filters
 * and add it to the token list (unless it's already there).
 */
int
tokenize_value(filter_pipeline_t *fp, tokenset_t *tokens,
    const char *val, size_t len, token_t **tokenp)
{
	filter_action_t action;
	token_t *token;

	token = token_create(val, len);
	if (__predict_false(token == NULL)) {
		return -1;
	}
	action = filter_pipeline_run(fp, &token->buffer);
	if (__predict_false(action != FILT_MUTATION)) {
		ASSERT(action == FILT_DISCARD || action == FILT_ERROR);
		token_destroy(token);
		if (action == FILT_ERROR) {
			return -1;
		}
		return 0;
	}
	*tokenp = tokenset_add(tokens, token);
	return 0;
}

/*
 * tokenize: uses ICU segmentation UBRK_WORD.
 *
 * See: https://unicode.org/reports/tr29/
 */
tokenset_t *
tokenize(filter_pipeline_t *fp, nxs_params_t *params,
    const char *text, size_t text_len)
{
	UBreakIterator *it_token = NULL;
	UErrorCode ec = U_ZERO_ERROR;
	UChar *utext = NULL;
	int32_t end, start, ulen;
	tokenset_t *tokens;
	strbuf_t buf;

	strbuf_init(&buf);

	if ((tokens = tokenset_create()) == NULL) {
		return NULL;
	}

	ulen = roundup2((text_len + 1) * 2, 64);
	if ((utext = malloc(ulen)) == NULL) {
		goto err;
	}
	if (utf8_to_utf16(NULL, text, utext, ulen) == -1) {
		goto err;
	}

	/*
	 * TODO: Use word brake rules to customize.  See:
	 *
	 * https://unicode-org.github.io/icu/userguide/boundaryanalysis/break-rules.html
	 * https://github.com/unicode-org/icu/blob/main/icu4c/source/data/brkitr/rules/word.txt
	 */
	it_token = ubrk_open(UBRK_WORD, nxs_params_get_str(params, "lang"),
	    utext, -1, &ec);
	if (__predict_false(U_FAILURE(ec))) {
		const char *errmsg __unused = u_errorName(ec);
		app_dbgx("ubrk_open() failed: %s", errmsg);
		goto err;
	}

	start = ubrk_first(it_token);
	for (end = ubrk_next(it_token); end != UBRK_DONE;
	    start = end, end = ubrk_next(it_token)) {
		token_t *token = NULL;

		ASSERT(start < end);

		if (ubrk_getRuleStatus(it_token) == UBRK_WORD_NONE) {
			continue;
		}

		if (utf8_from_utf16_new(NULL, utext + start,
		    end - start, &buf) == -1) {
			goto err;
		}

		if (tokenize_value(fp, tokens, buf.value, buf.length,
		    &token) == -1) {
			goto err;
		}
		ASSERT(token);
	}
err:
	if (it_token) {
		ubrk_close(it_token);
	}
	strbuf_release(&buf);
	free(utext);
	return tokens;
}
