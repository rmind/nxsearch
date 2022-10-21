/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Tokenizer
 *
 * - Processes the text splitting it into tokens separate by whitespace.
 * - Invokes the filter pipeline to process each token.
 * - Constructs a list of processed tokens.
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
 */
void
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
		return;
	}

	token->count = 1;
	TAILQ_INSERT_TAIL(&tset->list, token, entry);
	rhashmap_put(tset->map, str->value, str->length, token);

	tset->data_len += str->length;
	tset->count++;
	tset->seen++;
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
 * If found, associate it with the token; otherwise, move the token to
 * a separate staging list if the 'stage' flag is true.
 */
void
tokenset_resolve(tokenset_t *tset, nxs_index_t *idx, bool stage)
{
	token_t *token;

	token = TAILQ_FIRST(&tset->list);
	while (token) {
		token_t *next_token = TAILQ_NEXT(token, entry);
		const strbuf_t *sbuf = &token->buffer;
		idxterm_t *term;

		term = idxterm_lookup(idx, sbuf->value, sbuf->length);
		if (!term && stage) {
			TAILQ_REMOVE(&tset->list, token, entry);
			TAILQ_INSERT_TAIL(&tset->staging, token, entry);
			tset->staged++;
			app_dbgx("staging %p [%s]", token, sbuf->value);
		}
		if (term) {
			app_dbgx("[%s] => %u", sbuf->value, term->id);
		}
		token->idxterm = term;
		token = next_token;
	}
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
	tokenset_t *tset;
	strbuf_t buf;

	strbuf_init(&buf);

	if ((tset = tokenset_create()) == NULL) {
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
		filter_action_t action;
		token_t *token;

		/*
		 * Note: skip all boundary chars, spaces are not part
		 * of boundaries.
		 */
		if (ubrk_getRuleStatus(it_token) != UBRK_WORD_LETTER) {
			while (start < end) {
				if (ubrk_isBoundary(it_token, start + 1) ||
				    utext[start] == ' ') {
					start += 1;
					continue;
				}
				break;
			}
		}
		if (start == end) {
			continue;
		}

		if (utf8_from_utf16_new(NULL, utext + start,
		    end - start, &buf) == -1) {
			goto err;
		}

		token = token_create(buf.value, buf.length);
		if (__predict_false(token == NULL)) {
			goto err;
		}
		action = filter_pipeline_run(fp, &token->buffer);
		if (__predict_false(action != FILT_MUTATION)) {
			ASSERT(action == FILT_DROP || action == FILT_ERROR);
			token_destroy(token);
			if (action == FILT_ERROR) {
				goto err;
			}
			continue;
		}
		tokenset_add(tset, token);
	}
err:
	if (it_token) {
		ubrk_close(it_token);
	}
	strbuf_release(&buf);
	free(utext);
	return tset;
}
