/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Terms index.
 *
 * This module manages the terms list.  It is generally an append only
 * structure which follows the general idxmap synchronization logic.
 * Term IDs are determined by the term's order in the index.  Terms in
 * the list are NIL-terminated for convenience.  Each term has an total
 * occurrence count (in the whole index i.e. all documents) which is
 * accessed directly via the memory-mapped file.
 *
 * The count is a 64-bit integer updated atomically, therefore padding
 * must be added to provide the alignment where needed.
 *
 * See the storage.h header for more details on the on-disk layout.
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

	/*
	 * Setup the in-memory structures.
	 */
	idx->term_map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	if (idx->term_map == NULL) {
		goto err;
	}
	TAILQ_INIT(&idx->term_list);
	idx->terms_consumed = 0;
	idx->terms_last_id = 0;
	flock(fd, LOCK_UN);

	/*
	 * Finally, load the terms.
	 */
	return idx_terms_sync(idx);
err:
	flock(fd, LOCK_UN);
	idx_db_release(&idx->terms_memmap);
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

/*
 * idx_terms_add: add the given tokens/terms into the term index.
 *
 * - Tokens must be resolved using the idxterm_resolve_tokens() i.e. all
 * tokens which don't have the already existing in-memory term associated
 * must be in the staging list.
 *
 * - Terms are added into the on-disk index and the in-memory list.
 */
int
idx_terms_add(fts_index_t *idx, tokenset_t *tokens)
{
	idxmap_t *idxmap = &idx->terms_memmap;
	size_t max_append_len, data_len, target_len, append_len = 0;
	idxterms_hdr_t *hdr;
	token_t *token;
	void *dataptr;
	mmrw_t mm;
	int ret = -1;

	ASSERT(!TAILQ_EMPTY(&tokens->staging));
	ASSERT(tokens->count > 0);

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
	max_append_len = tokens->data_len +
	    (tokens->count * IDXTERMS_META_MAXLEN);
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

	while ((token = TAILQ_FIRST(&tokens->staging)) != NULL) {
		const char *val = token->buffer.value;
		const size_t len = token->buffer.length;
		idxterm_t *term;
		term_id_t id;
		size_t offset;

		/*
		 * De-duplicate: if the term is already present (because
		 * of the above re-sync), then just put it back to the list.
		 */
		term = rhashmap_get(idx->term_map, val, len);
		if (term) {
			TAILQ_REMOVE(&tokens->staging, token, entry);
			TAILQ_INSERT_TAIL(&tokens->list, token, entry);
			token->idxterm = term;
			continue;
		}

		if (mmrw_store16(&mm, len) == -1 ||
		    mmrw_store(&mm, val, len + 1) == -1 ||
		    mmrw_advance(&mm, IDXTERMS_PAD_LEN(len)) == -1) {
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
		term = idxterm_create(idx, val, len, offset);
		if (term == NULL) {
			goto err;
		}
		id = ++idx->terms_last_id;
		idxterm_assign(idx, term, id);
		token->idxterm = term;

		/* Token resolved: put it back to the list and iterate. */
		TAILQ_REMOVE(&tokens->staging, token, entry);
		TAILQ_INSERT_TAIL(&tokens->list, token, entry);
		append_len += IDXTERMS_BLK_LEN(len);
	}
	ASSERT(TAILQ_EMPTY(&tokens->staging));
	ret = 0;
err:
	/* Publish the new data length. */
	idx->terms_consumed = data_len + append_len;
	atomic_store_release(&hdr->data_len, htobe32(idx->terms_consumed));
	flock(idxmap->fd, LOCK_UN);
	return ret;
}

/*
 * idx_terms_sync: load any new terms from the on-disk index (by creating
 * the in-memory structures).
 */
int
idx_terms_sync(fts_index_t *idx)
{
	idxmap_t *idxmap = &idx->terms_memmap;
	size_t seen_data_len, target_len, consumed_len = 0;
	idxterms_hdr_t *hdr;
	void *dataptr;
	mmrw_t mm;
	int ret = -1;

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
		app_dbgx("nothing to consume", NULL);
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
	app_dbgx("consuming %zu", target_len);

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
			goto err;
		}

		/*
		 * Save the pointer to the term.  Advance to the
		 * term counter.  NOTE: There must be a NIL terminator,
		 * therefore we add 1 to the length.  Skip any padding.
		 */
		val = (const char *)mm.curptr;
		if (mmrw_advance(&mm, len + 1 + IDXTERMS_PAD_LEN(len)) == -1) {
			goto err;
		}
		offset = (uintptr_t)mm.curptr - (uintptr_t)hdr;

		if (mmrw_fetch64(&mm, &count) == -1) {
			goto err;
		}
		term = idxterm_create(idx, val, len, offset);
		if (term == NULL) {
			goto err;
		}
		id = ++idx->terms_last_id;
		idxterm_assign(idx, term, id);
		consumed_len += IDXTERMS_BLK_LEN(len);
	}
	ret = 0;
err:
	idx->terms_consumed = consumed_len;
	app_dbgx("consumed = %zu", consumed_len);
	return ret;
}
