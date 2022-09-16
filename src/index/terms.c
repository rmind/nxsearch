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

static int
idx_terms_init(idxmap_t *idxmap)
{
	idxterms_hdr_t *hdr = idxmap->baseptr;

	/*
	 * Initialize the header.  Issue a memory fence to ensure
	 * it reaches global visibility.
	 */
	memset(hdr, 0, sizeof(idxterms_hdr_t));
	memcpy(hdr->mark, NXS_T_MARK, sizeof(hdr->mark));
	hdr->ver = NXS_ABI_VER;
	atomic_store_release(&hdr->data_len, htobe32(0));
	return 0;
}

static int
idx_terms_verify(const idxmap_t *idxmap)
{
	const idxterms_hdr_t *hdr = idxmap->baseptr;
	const size_t flen = idxmap->mapped_len;

	if (memcmp(hdr->mark, NXS_T_MARK, sizeof(hdr->mark)) != 0 ||
	    hdr->ver != NXS_ABI_VER || IDXTERMS_FILE_LEN(hdr) > flen) {
		return -1;
	}
	return 0;
}

int
idx_terms_open(fts_index_t *idx, const char *path)
{
	int fd;
	void *baseptr;
	bool created;

	/*
	 * Open the index file.
	 *
	 * => Returns the descriptor with the lock held.
	 */
	if ((fd = idx_db_open(&idx->terms_memmap, path, &created)) == -1) {
		return -1;
	}

	/*
	 * Map and, if creating, initialize the header.
	 */
	baseptr = idx_db_map(&idx->terms_memmap, IDX_SIZE_STEP, false);
	if (baseptr == NULL) {
		goto err;
	}
	if (created && idx_terms_init(&idx->terms_memmap) == -1) {
		goto err;
	}
	if (!created && idx_terms_verify(&idx->terms_memmap) == -1) {
		goto err;
	}
	flock(fd, LOCK_UN);

	/*
	 * Setup the in-memory structures.
	 */
	idx->term_map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	TAILQ_INIT(&idx->term_list);
	idx->terms_consumed = 0;
	idx->terms_last_id = 0;

	/*
	 * Finally, load the terms.
	 */
	return idx_terms_sync(idx);
err:
	close(fd);
	return -1;
}

void
idx_terms_close(fts_index_t *idx)
{
	idxmap_t *idxmap = &idx->terms_memmap;
	idxterm_t *term;

	while ((term = TAILQ_FIRST(&idx->term_list)) != NULL) {
		idxterm_destroy(idx, term);
	}
	if (idx->term_map) {
		rhashmap_destroy(idx->term_map);
	}
	idx_db_release(idxmap);
}

int
idx_terms_add(fts_index_t *idx, tokenset_t *tokens)
{
	idxmap_t *idxmap = &idx->terms_memmap;
	size_t max_append_len, data_len, target_len;
	size_t append_len = 0;
	idxterms_hdr_t *hdr;
	token_t *token;
	void *dataptr;
	mmrw_t mm;

	/*
	 * Lock the file and check whether we need to sync.
	 */
	if (flock(idxmap->fd, LOCK_EX) == -1) {
		return -1;
	}
again:
	hdr = idxmap->baseptr;
	ASSERT(idxmap->fd > 0 && hdr != NULL);
	data_len = be32toh(atomic_load_acquire(&hdr->data_len));
	if (idx->terms_consumed < data_len) {
		/*
		 * Load new terms and try to calculate again as the
		 * file might have been re-mapped and the header pointer
		 * might have changed.
		 */
		if (idx_terms_sync(idx) == -1) {
			flock(idxmap->fd, LOCK_UN);
			return -1;
		}
		goto again;
	}

	/*
	 * Compute the target length and extend if necessary.
	 */
	max_append_len = tokens->data_len + (tokens->count * IDXTERMS_META_LEN);
	target_len = sizeof(idxterms_hdr_t) + data_len + max_append_len;
	if ((hdr = idx_db_map(idxmap, target_len, true)) == NULL) {
		flock(idxmap->fd, LOCK_UN);
		return -1;
	}

	/*
	 * Fill the terms (processed tokens).
	 */
	dataptr = MAP_GET_OFF(hdr, sizeof(idxterms_hdr_t) + data_len);
	mmrw_init(&mm, dataptr, max_append_len);

	TAILQ_FOREACH(token, &tokens->staging, entry) {
		const char *val = token->buffer.value;
		const size_t len = token->buffer.length;
		idxterm_t *term;
		term_id_t id;
		size_t offset;

		/*
		 * De-duplicate: if the term is already present (because
		 * of the above re-sync), then just increment the counter.
		 */
		term = rhashmap_get(idx->term_map, val, len);
		if (term) {
			token->idxterm = term;
			continue;
		}

		// FIXME: roundup2 term len for alignment

		if (mmrw_store16(&mm, len) == -1 ||
		    mmrw_store(&mm, val, len + 1) == -1) {
			goto err;
		}
		offset = (uintptr_t)mm.curptr - (uintptr_t)hdr;

		if (mmrw_store64(&mm, token->count) == -1) {
			goto err;
		}

		/*
		 * Create the term, assign the ID and also associate
		 * the token with it.
		 */
		if ((term = idxterm_create(idx, val, len, offset)) == NULL) {
			goto err;
		}
		id = ++idx->terms_last_id;
		idxterm_assign(idx, term, id);
		token->idxterm = term;

		append_len += len + IDXTERMS_META_LEN;
	}

	/* All tokens are now resolved; put them back to the list. */
	TAILQ_CONCAT(&tokens->list, &tokens->staging, entry);

	/* Publish the new data length. */
	idx->terms_consumed = data_len + append_len;
	atomic_store_release(&hdr->data_len, htobe32(idx->terms_consumed));
	flock(idxmap->fd, LOCK_UN);
	return 0;
err:
	// FIXME bump terms_consumed?
	flock(idxmap->fd, LOCK_UN);
	return -1;
}

int
idx_terms_sync(fts_index_t *idx)
{
	idxmap_t *idxmap = &idx->terms_memmap;
	size_t seen_data_len, target_len;
	idxterms_hdr_t *hdr;
	void *dataptr;
	mmrw_t mm;

	/*
	 * Fetch the data length.  Compute the length of data to consume.
	 */

	hdr = idxmap->baseptr;
	ASSERT(idxmap->fd > 0 && hdr != NULL);

	seen_data_len = be32toh(atomic_load_acquire(&hdr->data_len));
	if (seen_data_len == idx->terms_consumed) {
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
	hdr = idx_db_map(idxmap, sizeof(idxterms_hdr_t) + seen_data_len, false);
	if (hdr == NULL) {
		return -1;
	}
	target_len = seen_data_len - idx->terms_consumed;
	dataptr = MAP_GET_OFF(hdr, sizeof(idxterms_hdr_t) + idx->terms_consumed);

	/*
	 * Fetch the terms.
	 */
	mmrw_init(&mm, dataptr, target_len);
	while (mm.remaining) {
		idxterm_t *term;
		term_id_t id;
		const char *val;
		size_t offset;
		uint64_t count;
		uint16_t len;

		if (mmrw_fetch16(&mm, &len) == -1 || len == 0) {
			return -1;
		}

		/*
		 * Save the pointer to the term.  Advance to the
		 * term counter.  NOTE: There must be a NIL terminator,
		 * therefore we add 1 to the length.
		 */
		val = (const char *)mm.curptr;
		if (mmrw_advance(&mm, len + 1) == -1) {
			goto err;
		}
		offset = (uintptr_t)mm.curptr - (uintptr_t)hdr;

		if (mmrw_fetch64(&mm, &count) == -1) {
			goto err;
		}
		if ((term = idxterm_create(idx, val, len, offset)) == NULL) {
			goto err;
		}
		id = ++idx->terms_last_id;
		idxterm_assign(idx, term, id);
	}
	idx->terms_consumed = seen_data_len;
	return 0;
err:
	// FIXME bump terms_consumed?
	return -1;
}
