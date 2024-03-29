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
#include <stdbool.h>

#include "nxs.h"
#include "strbuf.h"
#include "rhashmap.h"
#include "filters.h"

#define	TOKENSET_STAGE		(0x01)
#define	TOKENSET_TRIM		(0x02)
#define	TOKENSET_FUZZYMATCH	(0x10)

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
	unsigned		staged;
	unsigned		seen;
} tokenset_t;

token_t *	token_create(const char *, size_t);
void		token_destroy(token_t *);

tokenset_t *	tokenset_create(void);
token_t *	tokenset_add(tokenset_t *, token_t *);
void		tokenset_moveback(tokenset_t *, token_t *);
void		tokenset_resolve(tokenset_t *, nxs_index_t *, unsigned);
void		tokenset_destroy(tokenset_t *);

int		tokenize_value(filter_pipeline_t *, tokenset_t *,
		    const char *, size_t, token_t **);
tokenset_t *	tokenize(filter_pipeline_t *, nxs_params_t *,
		    const char *, size_t);

#endif
