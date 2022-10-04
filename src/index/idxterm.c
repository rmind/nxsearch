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
 * - Tacks the documents where the term occurs, i.e. provides the
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
#include <errno.h>

#define	__NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "rhashmap.h"
#include "storage.h"
#include "index.h"
#include "utils.h"

int
idxterm_sysinit(nxs_index_t *idx)
{
	idx->td_map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	if (idx->td_map == NULL) {
		return -1;
	}
	return 0;
}

void
idxterm_sysfini(nxs_index_t *idx)
{
	if (idx->td_map) {
		rhashmap_destroy(idx->td_map);
	}
}

idxterm_t *
idxterm_create(nxs_index_t *idx, const char *token,
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
	idx->term_count++;

	app_dbgx("term %p [%s]", term, term->value);
	return term;
}

void
idxterm_destroy(nxs_index_t *idx, idxterm_t *term)
{
	const size_t term_len = strlen(term->value);

	TAILQ_REMOVE(&idx->term_list, term, entry);
	idx->term_count--;

	if (term->id) {
		rhashmap_del(idx->td_map, &term->id, sizeof(nxs_term_id_t));
	}
	rhashmap_del(idx->term_map, term->value, term_len);
	roaring_bitmap_free(term->doc_bitmap);
	free(term);
}

/*
 * idxterm_assign: assign the term ID and map the ID to the term object.
 */
void
idxterm_assign(nxs_index_t *idx, idxterm_t *term, nxs_term_id_t term_id)
{
	term->id = term_id;
	rhashmap_put(idx->td_map, &term->id, sizeof(nxs_term_id_t), term);
	app_dbgx("term %p [%s] => %u", term, term->value, term->id);
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
 * idxterm_lookup: find the term object given the term ID.
 */
idxterm_t *
idxterm_lookup_by_id(nxs_index_t *idx, nxs_term_id_t term_id)
{
	return rhashmap_get(idx->td_map, &term_id, sizeof(nxs_term_id_t));
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

		old_tc = *tc;
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

int
idxterm_del_doc(idxterm_t *term, nxs_doc_id_t doc_id)
{
	roaring_bitmap_remove(term->doc_bitmap, doc_id);
	app_dbgx("unlinking doc %"PRIu64" from term %u", doc_id, term->id);
	return 0;
}
