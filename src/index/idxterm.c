/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * In-memory term and term-document mapping.
 *
 * - Tracks term IDs and provides the mapping to the term values.
 *
 * - Resolves (associates) tokens to the term objects which contain
 * the term IDs and other metadata.
 *
 * - Tracks the documents where the term occurs, i.e. provides the
 * following mapping: term_id => [doc IDs ...].
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

#define	__NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "rhashmap.h"
#include "storage.h"
#include "index.h"
#include "utils.h"

static int	idxterm_levdist(void *, const void *, const void *);

int
idxterm_sysinit(nxs_index_t *idx)
{
	TAILQ_INIT(&idx->term_list);

	idx->term_map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	if (idx->term_map == NULL) {
		goto err;
	}

	idx->td_map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	if (idx->td_map == NULL) {
		goto err;
	}

	idx->term_levctx = levdist_create();
	if (idx->term_levctx == NULL) {
		goto err;
	}

	idx->term_bkt = bktree_create(idxterm_levdist, idx);
	if (idx->term_bkt == NULL) {
		goto err;
	}
	return 0;
err:
	idxterm_sysfini(idx);
	return -1;
}

void
idxterm_sysfini(nxs_index_t *idx)
{
	idxterm_t *term;

	while ((term = TAILQ_FIRST(&idx->term_list)) != NULL) {
		idxterm_destroy(idx, term);
	}
	if (idx->term_map) {
		rhashmap_destroy(idx->term_map);
	}
	if (idx->td_map) {
		rhashmap_destroy(idx->td_map);
	}
	if (idx->term_bkt) {
		bktree_destroy(idx->term_bkt);
	}
	if (idx->term_levctx) {
		levdist_destroy(idx->term_levctx);
	}
}

static int
idxterm_levdist(void *ctx, const void *a, const void *b)
{
	const idxterm_t *term_a = a;
	const idxterm_t *term_b = b;
	nxs_index_t *idx = ctx;

	return levdist(idx->term_levctx, term_a->value, term_a->value_len,
	    term_b->value, term_b->value_len);
}

idxterm_t *
idxterm_create(const char *token, const size_t len, const size_t offset)
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

	ASSERT(len <= UINT16_MAX);
	term->value_len = len;

	return term;
}

void
idxterm_destroy(nxs_index_t *idx, idxterm_t *term)
{
	if (term->id) {
		/*
		 * XXX: bktree_delete
		 *
		 * Currently, idxterm_destroy() is called only when
		 * closing the index and there are no individual deletions.
		 * Nevertheless, the API should not leave the stray pointers
		 * in the tree.
		 */
		rhashmap_del(idx->td_map, &term->id, sizeof(nxs_term_id_t));
		rhashmap_del(idx->term_map, term->value, term->value_len);
		TAILQ_REMOVE(&idx->term_list, term, entry);
		idx->term_count--;
	}
	roaring_bitmap_free(term->doc_bitmap);
	free(term);
}

/*
 * idxterm_insert: map the term to the value and term ID to the object.
 *
 * => Returns the inserted term on success.
 * => Returns already existing term, if it exists.
 * => Returns NULL on failure.
 */
idxterm_t *
idxterm_insert(nxs_index_t *idx, idxterm_t *term, nxs_term_id_t term_id)
{
	const size_t len = term->value_len;
	idxterm_t *result_term;

	/*
	 * Map the term/token value to the object.
	 */
	result_term = rhashmap_put(idx->term_map, term->value, len, term);
	if (result_term != term) {
		/* Error: the index contains a duplicate. */
		app_dbgx("duplicate term [%s] in the map", term->value);
		return result_term;
	}
	if (bktree_insert(idx->term_bkt, term) == -1) {
		app_dbgx("bktree_insert on term [%s] failed", term->value);
		rhashmap_del(idx->term_map, term->value, len);
		return NULL;
	}
	TAILQ_INSERT_TAIL(&idx->term_list, term, entry);
	idx->term_count++;

	/*
	 * Assign the term ID and map the ID to the term object.
	 */
	term->id = term_id;
	rhashmap_put(idx->td_map, &term->id, sizeof(nxs_term_id_t), term);
	app_dbgx("term %p [%s] => %u", term, term->value, term->id);
	return term;
}

/*
 * idxterm_lookup: find the term object given the term/token value.
 */
idxterm_t *
idxterm_lookup(nxs_index_t *idx, const char *value, size_t len)
{
	return rhashmap_get(idx->term_map, value, len);
}

/*
 * idxterm_lookup_by_id: find the term object given the term ID.
 */
idxterm_t *
idxterm_lookup_by_id(nxs_index_t *idx, nxs_term_id_t term_id)
{
	return rhashmap_get(idx->td_map, &term_id, sizeof(nxs_term_id_t));
}

/*
 * idxterm_fuzzysearch: perform a fuzzy match search.
 */
idxterm_t *
idxterm_fuzzysearch(nxs_index_t *idx, const char *value, size_t len)
{
	idxterm_t *search_token, *term = NULL, *iterm;
	deque_t *results = NULL;
	uint64_t term_total = 0;
	unsigned total_len;

	/* XXX: inefficient (alloc + copy) */
	total_len = offsetof(idxterm_t, value[(unsigned)len + 1]);
	if ((search_token = malloc(total_len)) == NULL) {
		return NULL;
	}
	memcpy(search_token->value, value, len);
	search_token->value[len] = '\0';
	search_token->value_len = len;

	if ((results = deque_create(0, 0)) == NULL) {
		goto out;
	}
	if (bktree_search(idx->term_bkt, LEVDIST_TOLERANCE,
	    search_token, results) == -1) {
		goto out;
	}

	/*
	 * Select the most popular term.
	 */
	while ((iterm = deque_pop_back(results)) != NULL) {
		if (idxterm_get_total(idx, iterm) > term_total) {
			term = iterm;
		}
	}
out:
	if (results) {
		deque_destroy(results);
	}
	free(search_token);
	return term;
}

uint64_t
idxterm_get_total(nxs_index_t *idx, const idxterm_t *term)
{
	const idxmap_t *idxmap = &idx->terms_memmap;
	const idxterms_hdr_t *hdr = idxmap->baseptr;
	uint64_t *tc = MAP_GET_OFF(hdr, term->offset);

	ASSERT(ALIGNED_POINTER(tc, uint64_t));
	return be64toh(atomic_load_relaxed(tc));
}

/*
 * idxterm_{incr,decr}_total: helpers to increment or decrement the
 * term counters (for total term occurrence).
 */

void
idxterm_incr_total(nxs_index_t *idx, const idxterm_t *term, unsigned count)
{
	const idxmap_t *idxmap = &idx->terms_memmap;
	const idxterms_hdr_t *hdr = idxmap->baseptr;
	uint64_t *tc = MAP_GET_OFF(hdr, term->offset);
	uint64_t old_tc, new_tc;

	ASSERT(ALIGNED_POINTER(tc, uint64_t));

	do {
		old_tc = *tc;
		new_tc = htobe64(be64toh(old_tc) + count);
	} while (!atomic_cas_relaxed(tc, &old_tc, new_tc));

	app_dbgx("term %u count +%u ", term->id, count);
}

void
idxterm_decr_total(nxs_index_t *idx, const idxterm_t *term, unsigned count)
{
	const idxmap_t *idxmap = &idx->terms_memmap;
	const idxterms_hdr_t *hdr = idxmap->baseptr;
	uint64_t *tc = MAP_GET_OFF(hdr, term->offset);
	uint64_t old_tc, new_tc;

	ASSERT(ALIGNED_POINTER(tc, uint64_t));

	do {
		uint64_t old_tc_val;

		old_tc = atomic_load_relaxed(tc);
		old_tc_val = be64toh(old_tc);
		if (old_tc_val < count) {
			/*
			 * This should never happen, but nevertheless we do
			 * not want to overflow in the event of inconsistency.
			 */
			app_dbgx("term %u count -%u ", term->id, count);
			return;
		}
		new_tc = htobe64(old_tc_val - count);

	} while (!atomic_cas_relaxed(tc, &old_tc, new_tc));

	app_dbgx("term %u count -%u ", term->id, count);
}

int
idxterm_add_doc(idxterm_t *term, nxs_doc_id_t doc_id)
{
	roaring_bitmap_add(term->doc_bitmap, doc_id);
	app_dbgx("term %u => doc %"PRIu64, term->id, doc_id);
	return 0;
}

void
idxterm_del_doc(idxterm_t *term, nxs_doc_id_t doc_id)
{
	roaring_bitmap_remove(term->doc_bitmap, doc_id);
	app_dbgx("unlinking doc %"PRIu64" from term %u", doc_id, term->id);
}
