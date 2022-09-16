/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _TOKENIZER_H_
#define _TOKENIZER_H_

#include <sys/queue.h>
#include <inttypes.h>

#include "strbuf.h"
#include "rhashmap.h"
#include "filters.h"

struct idxmap;

typedef struct token {
	/*
	 * Token: list entry, counter of how many times the token
	 * was seen and the string buffer storing the value of
	 * after the filter pipeline processing.
	 */
	TAILQ_ENTRY(token)	entry;
	struct idxterm *	idxterm;
	unsigned		count;
	strbuf_t		buffer;
} token_t;

typedef struct {
	/*
	 * Token list and a map for counting the unique tokens.
	 */
	TAILQ_HEAD(, token)	list;
	rhashmap_t *		map;

	/* Staging list for tokens which are not in the index. */
	TAILQ_HEAD(, token)	staging;

	/*
	 * Total token data length (sum of string lengths, excluding
	 * the NIL terminator), the number of tokens and the total number
	 * of tokens seen (including duplicates).
	 */
	size_t			data_len;
	unsigned		count;
	unsigned		seen;
} tokenset_t;

tokenset_t *	tokenset_create(void);
void		tokenset_add(tokenset_t *, token_t *);
void		tokenset_destroy(tokenset_t *);

token_t *	token_create(const char *, size_t);
void		token_destroy(token_t *);

tokenset_t *	tokenize(filter_pipeline_t *, const char *, size_t);

#endif
