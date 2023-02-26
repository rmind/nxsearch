/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * In-memory document mapping.
 *
 * Tracks document IDs and provides the mapping to the document metadata
 * in the on-disk index.  It is used to get the terms and their counts
 * associated with the documents.
 */

#include <sys/queue.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>

#define	__NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "rhashmap.h"
#include "storage.h"
#include "index.h"
#include "mmrw.h"
#include "utils.h"

idxdoc_t *
idxdoc_create(nxs_index_t *idx, nxs_doc_id_t id, uint64_t offset)
{
	idxdoc_t *doc;

	doc = malloc(sizeof(idxdoc_t));
	if (doc == NULL) {
		return NULL;
	}
	doc->id = id;
	doc->offset = offset;

	if (rhashmap_put(idx->dt_map, &doc->id,
	    sizeof(nxs_doc_id_t), doc) != doc) {
		free(doc);
		errno = EEXIST;
		return NULL;
	}
	TAILQ_INSERT_TAIL(&idx->dt_list, doc, entry);
	idx->dt_count++;

	app_dbgx("doc ID %"PRIu64" at %"PRIu64" => %p", id, offset, doc);
	return doc;
}

void
idxdoc_destroy(nxs_index_t *idx, idxdoc_t *doc)
{
	rhashmap_del(idx->dt_map, &doc->id, sizeof(nxs_doc_id_t));
	TAILQ_REMOVE(&idx->dt_list, doc, entry);
	idx->dt_count--;
	app_dbgx("doc ID %"PRIu64" (%p), total %lu",
	    doc->id, doc, idx->dt_count);
	free(doc);
}

idxdoc_t *
idxdoc_lookup(nxs_index_t *idx, nxs_doc_id_t doc_id)
{
	idxdoc_t *doc;

	doc = rhashmap_get(idx->dt_map, &doc_id, sizeof(nxs_doc_id_t));
	app_dbgx("doc ID %"PRIu64" => %p", doc_id, doc);
	return doc;
}

/*
 * idxdoc_get_doclen: get the document length in tokens.
 */
int
idxdoc_get_doclen(const nxs_index_t *idx, const idxdoc_t *doc)
{
	const idxmap_t *idxmap = &idx->dt_memmap;
	const idxdt_hdr_t *hdr = idxmap->baseptr;
	unsigned n;
	mmrw_t mm;

	mmrw_init(&mm, MAP_GET_OFF(hdr, doc->offset),
	    (sizeof(idxdt_hdr_t) + idx->dt_consumed) - doc->offset);

	if (mmrw_advance(&mm, 8) == -1 ||
	    mmrw_fetch32(&mm, &n) == -1) {
		return -1;
	}
	return (int)n;
}

/*
 * idxdoc_get_termcount: get the term count (given the term ID)
 * in the given document.
 */
int
idxdoc_get_termcount(const nxs_index_t *idx,
    const idxdoc_t *doc, nxs_term_id_t term_id)
{
	const idxmap_t *idxmap = &idx->dt_memmap;
	const idxdt_hdr_t *hdr = idxmap->baseptr;
	const uint32_t *termblocks;
	unsigned n, off = 0;
	mmrw_t mm;

	mmrw_init(&mm, MAP_GET_OFF(hdr, doc->offset),
	    (sizeof(idxdt_hdr_t) + idx->dt_consumed) - doc->offset);

	if (mmrw_advance(&mm, 8 + 4) == -1 ||
	    mmrw_fetch32(&mm, &n) == -1) {
		return -1;
	}
	termblocks = (const void *)mm.curptr;

	/*
	 * The algorithm is inspired by the BSD bsearch(3):
	 * - Half the range on each move to the left or right.
	 * - Set the offset to the next after pivot when moving right.
	 */
	while (n) {
		unsigned i = off + (n >> 1);
		nxs_term_id_t target_term_id = be32toh(termblocks[i * 2]);

		if (term_id == target_term_id) {
			/* Match: return the count. */
			return be32toh(termblocks[i * 2 + 1]);
		}
		if (term_id > target_term_id) {
			/* Move right */
			off = i + 1;
			n--;
		} else {
			/* Moving left */
		}
		n >>= 1;
	}
	return -1;
}
