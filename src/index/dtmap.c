/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/file.h>

#include <stdlib.h>
#include <stddef.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define	__NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "tokenizer.h"
#include "storage.h"
#include "rhashmap.h"
#include "index.h"
#include "mmrw.h"
#include "utils.h"

static idxdoc_t *	idxdoc_create(fts_index_t *, doc_id_t, uint64_t);
static void		idxdoc_destroy(fts_index_t *, idxdoc_t *);

static int
idx_dtmap_init(idxmap_t *idxmap)
{
	idxdt_hdr_t *hdr = idxmap->baseptr;

	/*
	 * Initialize the header.  Issue a memory fence to ensure
	 * it reaches global visibility.
	 */
	memset(hdr, 0, sizeof(idxdt_hdr_t));
	memcpy(hdr->mark, NXS_M_MARK, sizeof(hdr->mark));
	hdr->ver = NXS_ABI_VER;
	atomic_store_release(&hdr->data_len, htobe32(0));
	return 0;
}

static int
idx_dtmap_verify(const idxmap_t *idxmap)
{
	const idxdt_hdr_t *hdr = idxmap->baseptr;
	const size_t flen = idxmap->mapped_len;

	if (memcmp(hdr->mark, NXS_M_MARK, sizeof(hdr->mark)) != 0 ||
	    hdr->ver != NXS_ABI_VER || IDXDT_FILE_LEN(hdr) > flen) {
		return -1;
	}
	return 0;
}

int
idx_dtmap_open(fts_index_t *idx, const char *path)
{
	int fd;
	void *baseptr;
	bool created;

	/*
	 * Open the index file.
	 *
	 * => Returns the descriptor with the lock held.
	 */
	if ((fd = idx_db_open(&idx->dt_memmap, path, &created)) == -1) {
		return -1;
	}

	/*
	 * Map and, if creating, initialize the header.
	 */
	baseptr = idx_db_map(&idx->dt_memmap, IDX_SIZE_STEP, false);
	if (baseptr == NULL) {
		goto err;
	}
	if (created && idx_dtmap_init(&idx->dt_memmap) == -1) {
		goto err;
	}
	if (!created && idx_dtmap_verify(&idx->dt_memmap) == -1) {
		goto err;
	}
	flock(fd, LOCK_UN);

	/*
	 * Setup the in-memory structures.
	 */
	idx->dt_map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	TAILQ_INIT(&idx->dt_list);
	idx->dt_consumed = 0;

	/*
	 * Finally, load the map.
	 */
	return idx_dtmap_sync(idx);
err:
	close(fd);
	return -1;
}

void
idx_dtmap_close(fts_index_t *idx)
{
	idxmap_t *idxmap = &idx->dt_memmap;
	idxdoc_t *doc;

	while ((doc = TAILQ_FIRST(&idx->dt_list)) != NULL) {
		idxdoc_destroy(idx, doc);
	}
	if (idx->dt_map) {
		rhashmap_destroy(idx->dt_map);
	}
	idx_db_release(idxmap);
}

int
idx_dtmap_add(fts_index_t *idx, doc_id_t id, tokenset_t *tokens)
{
	idxmap_t *idxmap = &idx->dt_memmap;
	size_t append_len, data_len, target_len, offset;
	idxdt_hdr_t *hdr;
	token_t *token;
	void *dataptr;
	mmrw_t mm;

	ASSERT(id > 0);
	ASSERT(!TAILQ_EMPTY(&tokens->list));

	/*
	 * Lock the file and remap/extend if necessary.
	 */
	if (flock(idxmap->fd, LOCK_EX) == -1) {
		return -1;
	}
again:
	hdr = idxmap->baseptr;
	ASSERT(idxmap->fd > 0 && hdr != NULL);
	data_len = be64toh(atomic_load_acquire(&hdr->data_len));
	if (idx->dt_consumed < data_len) {
		if (idx_dtmap_sync(idx) == -1) {
			flock(idxmap->fd, LOCK_UN);
			return -1;
		}
		goto again;
	}

	/*
	 * Compute the target length and extend if necessary.
	 */
	append_len = IDXDT_META_LEN(tokens->count);
	target_len = sizeof(idxdt_hdr_t) + data_len + append_len;
	if ((hdr = idx_db_map(idxmap, target_len, true)) == NULL) {
		flock(idxmap->fd, LOCK_UN);
		return -1;
	}

	dataptr = MAP_GET_OFF(hdr, sizeof(idxdt_hdr_t) + data_len);
	mmrw_init(&mm, dataptr, append_len);

	/*
	 * Add the document to the in-memory map.
	 */
	offset = (uintptr_t)mm.curptr - (uintptr_t)hdr;
	if (!idxdoc_create(idx, id, offset)) {
		flock(idxmap->fd, LOCK_UN);
		return -1;
	}

	/*
	 * Fill the document metadata.
	 */
	if (mmrw_store64(&mm, id) == -1 ||
	    mmrw_store32(&mm, tokens->seen) == -1 ||
	    mmrw_store32(&mm, tokens->count) == -1) {
		goto err;
	}

	/*
	 * Fill the terms seen in the document.
	 */
	TAILQ_FOREACH(token, &tokens->list, entry) {
		const idxterm_t *idxterm = token->idxterm;

		/* The term must be resolved. */
		ASSERT(idxterm != NULL);
		ASSERT(idxterm->id > 0);

		if (mmrw_store32(&mm, idxterm->id) == -1 ||
		    mmrw_store32(&mm, token->count) == -1) {
			goto err;
		}
		// FIXME: idxterm_incr_total(idx, idxterm, token->count);
	}

	/*
	 * Increment the document totals and publish the new data length.
	 */
	idx->dt_consumed = data_len + append_len;
	hdr->total_tokens = htobe64(be64toh(hdr->total_tokens) + tokens->seen);
	hdr->doc_count = htobe32(be32toh(hdr->doc_count) + 1);
	atomic_store_release(&hdr->data_len, htobe64(idx->dt_consumed));
	flock(idxmap->fd, LOCK_UN);
	return 0;
err:
	flock(idxmap->fd, LOCK_UN);
	// XXX: decrement the counters?
	// FIXME: free(doc);
	return -1;
}

int
idx_dtmap_sync(fts_index_t *idx)
{
	idxmap_t *idxmap = &idx->dt_memmap;
	size_t seen_data_len, target_len;
	idxdt_hdr_t *hdr;
	void *dataptr;
	mmrw_t mm;

	hdr = idxmap->baseptr;
	ASSERT(idxmap->fd > 0 && hdr != NULL);

	/*
	 * Fetch the data length.  Compute the length of data to consume.
	 */
	seen_data_len = be64toh(atomic_load_acquire(&hdr->data_len));
	if (seen_data_len == idx->dt_consumed) {
		/*
		 * No new data: there is nothing to do.
		 */
		return 0;
	}

	/*
	 * Ensure mapping: verify that it does not exceed the mapping
	 * length or re-maps using the new length.
	 *
	 * We will consume from the last processed data offset.
	 */
	hdr = idx_db_map(idxmap, sizeof(idxdt_hdr_t) + seen_data_len, false);
	if (hdr == NULL) {
		return -1;
	}
	target_len = seen_data_len - idx->dt_consumed;
	dataptr = MAP_GET_OFF(hdr, sizeof(idxdt_hdr_t) + idx->dt_consumed);

	/*
	 * Fetch the document mapping.
	 */
	mmrw_init(&mm, dataptr, target_len);
	while (mm.remaining) {
		doc_id_t doc_id;
		uint32_t n, doc_total_len;
		uint64_t offset;

		offset = (uintptr_t)mm.curptr - (uintptr_t)hdr;

		if (mmrw_fetch64(&mm, &doc_id) == -1 ||
		    mmrw_fetch32(&mm, &doc_total_len) == -1 ||
		    mmrw_fetch32(&mm, &n) == -1) {
			return -1;
		}
		if (!idxdoc_create(idx, doc_id, offset)) {
			return -1;
		}

		/*
		 * Build the reverse term-document index.
		 */
		for (unsigned i = 0; i < n; i++) {
			term_id_t id;
			uint32_t count;

			if (mmrw_fetch32(&mm, &id) == -1 ||
			    mmrw_fetch32(&mm, &count) == -1) {
				return -1;
			}
			if (idxterm_add_doc(idx, id, doc_id) == -1) {
				return -1;
			}
		}
	}
	idx->dt_consumed = seen_data_len;
	return 0;
}

static idxdoc_t *
idxdoc_create(fts_index_t *idx, doc_id_t id, uint64_t offset)
{
	idxdoc_t *doc;

	doc = malloc(sizeof(idxdoc_t));
	if (doc == NULL) {
		return NULL;
	}
	doc->id = id;
	doc->offset = offset;

	if (rhashmap_put(idx->dt_map, &doc->id, sizeof(doc_id_t), doc) != doc) {
		free(doc);
		errno = EINVAL;
		return NULL;
	}
	TAILQ_INSERT_TAIL(&idx->dt_list, doc, entry);
	return doc;
}

static void
idxdoc_destroy(fts_index_t *idx, idxdoc_t *doc)
{
	rhashmap_del(idx->dt_map, &doc->id, sizeof(doc_id_t));
	TAILQ_REMOVE(&idx->dt_list, doc, entry);
	free(doc);
}

int
idxdoc_get_termcount(fts_index_t *idx, doc_id_t doc_id, term_id_t term_id)
{
	const idxmap_t *idxmap = &idx->dt_memmap;
	const idxdt_hdr_t *hdr = idxmap->baseptr;
	idxdoc_t *doc;
	unsigned n;
	mmrw_t mm;

	doc = rhashmap_get(idx->dt_map, &doc_id, sizeof(doc_id_t));
	if (doc == NULL) {
		return -1;
	}

	mmrw_init(&mm, MAP_GET_OFF(hdr, doc->offset),
	    IDXDT_FILE_LEN(hdr) - doc->offset);

	if (mmrw_advance(&mm, 8 + 4) == -1 ||
	    mmrw_fetch32(&mm, &n) == -1) {
		return -1;
	}

	/*
	 * XXX: O(n) scan; sort by term ID on indexing and use
	 * binary search here?
	 */
	for (unsigned i = 0; i < n; i++) {
		term_id_t id;
		uint32_t count;

		if (mmrw_fetch32(&mm, &id) == -1 ||
		    mmrw_fetch32(&mm, &count) == -1) {
			return -1;
		}
		if (term_id == id) {
			return count;
		}
	}

	return -1;
}
