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

#include "tokenizer.h"
#include "index.h"
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
 * tokenize: split the text into tokens separated by a whitespace
 * and pass the token through the filter pipeline.
 */
tokenset_t *
tokenize(filter_pipeline_t *fp, const char *text, size_t text_len __unused)
{
	const char *sep = ",.;:| \t\n";
	char *content, *val, *brk;
	tokenset_t *tset;

	if ((tset = tokenset_create()) == NULL) {
		return NULL;
	}
	if ((content = strdup(text)) == NULL) {
		goto err;
	}

	for (val = strtok_r(content, sep, &brk); val;
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
	free(content);
	return tset;
err:
	tokenset_destroy(tset);
	free(content);
	return NULL;
}
