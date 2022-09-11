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

#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>

#include "strbuf.h"
#include "filters.h"
#include "tokenizer.h"
#include "utf8.h"
#include "utils.h"

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
	tset->map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	return tset;
}

void
tokenset_destroy(tokenset_t *tset)
{
	token_t *token;

	while ((token = TAILQ_FIRST(&tset->list)) != NULL) {
		TAILQ_REMOVE(&tset->list, token, entry);
		token_destroy(token);
	}
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
 * tokenize: split the text into tokens separated by a whitespace
 * and pass the token through the filter pipeline.
 */
tokenset_t *
tokenize(filter_pipeline_t *fp, char *text, size_t text_len __unused)
{
	const char *sep = " \t\n";
	char *val, *brk;
	tokenset_t *tset;

	if ((tset = tokenset_create()) == NULL) {
		return NULL;
	}

	for (val = strtok_r(text, sep, &brk); val;
	    val = strtok_r(NULL, sep, &brk)) {
		const size_t len = strlen(val);  // XXX
		filter_action_t action;
		token_t *token;

		token = token_create(val, len);
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
	return tset;
err:
	tokenset_destroy(tset);
	return NULL;
}
