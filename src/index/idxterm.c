/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <sys/queue.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>

#define	__NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "rhashmap.h"
#include "storage.h"
#include "index.h"
#include "utils.h"

int
idxterm_sysinit(fts_index_t *idx)
{
	idx->td_map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	if (idx->td_map == NULL) {
		return -1;
	}
	return 0;
}

void
idxterm_sysfini(fts_index_t *idx)
{
	if (idx->td_map) {
		rhashmap_destroy(idx->td_map);
	}
}

idxterm_t *
idxterm_create(fts_index_t *idx, const char *token,
    const size_t len, const size_t offset)
{
	idxterm_t *term;
	size_t total_len;

	/*
	 * Allocate and setup the in-memory term object.
	 */
	total_len = offsetof(idxterm_t, value[(unsigned)len + 1]);
	if ((term = malloc(total_len)) == NULL) {
		return NULL;
	}
	term->id = 0;
	term->doc_bitmap = roaring_bitmap_create();
	term->offset = offset;

	memcpy(term->value, token, len);
	term->value[len] = '\0';

	/*
	 * Map the term/token value to the object.
	 */
	if (rhashmap_put(idx->term_map, term->value, len, term) != term) {
		/* Error: the index contains a duplicate. */
		free(term);
		return NULL;
	}
	TAILQ_INSERT_TAIL(&idx->term_list, term, entry);
	return term;
}

void
idxterm_destroy(fts_index_t *idx, idxterm_t *term)
{
	const size_t term_len = strlen(term->value);

	TAILQ_REMOVE(&idx->term_list, term, entry);
	if (term->id) {
		(void)rhashmap_del(idx->td_map, &term->id, sizeof(term_id_t));
	}
	(void)rhashmap_del(idx->term_map, term->value, term_len);
	roaring_bitmap_free(term->doc_bitmap);
	free(term);
}

/*
 * idxterm_assign: assign the term ID and map the ID to the term object.
 */
void
idxterm_assign(fts_index_t *idx, idxterm_t *term, term_id_t term_id)
{
	term->id = term_id;
	rhashmap_put(idx->td_map, &term->id, sizeof(term_id_t), term);
}

/*
 * idxterm_lookup: find the term object given the term/token value.
 */
idxterm_t *
idxterm_lookup(fts_index_t *idx, const char *value, size_t len)
{
	return rhashmap_get(idx->term_map, value, len);
}

/*
 * idxterm_resolve_tokens: lookup the in-memory term object for each token.
 * If found, associate it with the token; otherwise, move the token to
 * a separate staging list if the 'stage' flag is true.
 */
void
idxterm_resolve_tokens(fts_index_t *idx, tokenset_t *tokens, bool stage)
{
	token_t *token;

	TAILQ_FOREACH(token, &tokens->list, entry) {
		const strbuf_t *sbuf = &token->buffer;
		idxterm_t *term;

		term = idxterm_lookup(idx, sbuf->value, sbuf->length);
		if (!term && stage) {
			TAILQ_REMOVE(&tokens->list, token, entry);
			TAILQ_INSERT_TAIL(&tokens->staging, token, entry);
		}
		token->idxterm = term;
	}
}

void
idxterm_incr_total(fts_index_t *idx, const idxterm_t *term, unsigned count)
{
	const idxmap_t *idxmap = &idx->terms_memmap;
	const idxterms_hdr_t *hdr = idxmap->baseptr;
	uint64_t *tc = MAP_GET_OFF(hdr, term->offset);
#if 0 // FIXME
	uint64_t old_tc, new_tc;

	do {
		old_tc = be64toh(*tc);
		new_tc = htobe64(old_tc + 1);
	} while (!atomic_compare_exchange_weak_explicit(&tc, &old_tc, new_tc,
	    memory_order_relaxed, memory_order_relaxed));
#endif
	atomic_fetch_add_explicit(tc, count, memory_order_relaxed);
}

int
idxterm_add_doc(fts_index_t *idx, term_id_t term_id, doc_id_t doc_id)
{
	idxterm_t *term;

	term = rhashmap_get(idx->td_map, &term_id, sizeof(term_id_t));
	if (!term) {
		return -1;
	}
	roaring_bitmap_add(term->doc_bitmap, doc_id);
	return 0;
}
