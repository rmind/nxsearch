/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Document-term index.
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
idx_dtmap_init(idxmap_t *idxmap)
{
	idxdt_hdr_t *hdr = idxmap->baseptr;

	/*
	 * Initialize the header.  Issue a memory fence to ensure
	 * it reaches global visibility.
	 */
	memset(hdr, 0, sizeof(idxdt_hdr_t));
	memcpy(hdr->mark, NXS_D_MARK, sizeof(hdr->mark));
	hdr->ver = NXS_ABI_VER;
	atomic_store_release(&hdr->data_len, htobe32(0));
	return 0;
}

static int
idx_dtmap_verify(const idxmap_t *idxmap)
{
	const idxdt_hdr_t *hdr = idxmap->baseptr;

	if (memcmp(hdr->mark, NXS_D_MARK, sizeof(hdr->mark)) != 0 ||
	    hdr->ver != NXS_ABI_VER) {
		return -1;
	}
	return 0;
}

int
idx_dtmap_open(nxs_index_t *idx, const char *path)
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

	/*
	 * Setup the in-memory structures.
	 */
	idx->dt_map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	if (idx->dt_map == NULL) {
		goto err;
	}
	TAILQ_INIT(&idx->dt_list);
	idx->dt_consumed = 0;
	flock(fd, LOCK_UN);

	/*
	 * Finally, load the map.
	 */
	return idx_dtmap_sync(idx);
err:
	flock(fd, LOCK_UN);
	idx_db_release(&idx->dt_memmap);
	return -1;
}

void
idx_dtmap_close(nxs_index_t *idx)
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
idx_dtmap_add(nxs_index_t *idx, nxs_doc_id_t doc_id, tokenset_t *tokens)
{
	idxmap_t *idxmap = &idx->dt_memmap;
	size_t append_len, data_len, target_len, offset;
	idxdoc_t *doc = NULL;
	token_t *token = NULL;
	idxdt_hdr_t *hdr;
	void *dataptr;
	mmrw_t mm;

	ASSERT(doc_id > 0);
	ASSERT(!TAILQ_EMPTY(&tokens->list));
	ASSERT(TAILQ_EMPTY(&tokens->staging));

	app_dbgx("processing %u tokens", tokens->count);
	ASSERT(tokens->count > 0);

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
	if (idxdoc_lookup(idx, doc_id)) {
		app_dbgx("document %"PRIu64" is already indexed", doc_id);
		flock(idxmap->fd, LOCK_UN);
		return -1;
	}

	/*
	 * Compute the target length and extend if necessary.
	 */
	append_len = IDXDT_META_LEN(tokens->count);
	target_len = sizeof(idxdt_hdr_t) + data_len + append_len;
	if ((hdr = idx_db_map(idxmap, target_len, true)) == NULL) {
		goto err;
	}

	dataptr = IDXDT_DATA_PTR(hdr, data_len);
	mmrw_init(&mm, dataptr, append_len);

	/*
	 * Add the document to the in-memory map.
	 */
	offset = (uintptr_t)mm.curptr - (uintptr_t)hdr;
	if ((doc = idxdoc_create(idx, doc_id, offset)) == NULL) {
		goto err;
	}

	/*
	 * Fill the document metadata.
	 */
	if (mmrw_store64(&mm, doc_id) == -1 ||
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
		if (idxterm_add_doc(idx, idxterm->id, doc_id) == -1) {
			goto err;
		}
		idxterm_incr_total(idx, idxterm, token->count);
	}

	/*
	 * Increment the document totals and publish the new data length.
	 */
	idx->dt_consumed = data_len + append_len;
	atomic_store_relaxed(&hdr->token_count,
	    htobe64(IDXDT_TOKEN_COUNT(hdr) + tokens->seen));
	atomic_store_relaxed(&hdr->doc_count,
	    htobe32(IDXDT_DOC_COUNT(hdr) + 1));
	atomic_store_release(&hdr->data_len, htobe64(idx->dt_consumed));

	if (idxmap->sync) {
		msync(hdr, target_len, MS_ASYNC);
	}
	flock(idxmap->fd, LOCK_UN);

	return 0;
err:
	flock(idxmap->fd, LOCK_UN);
	if (doc) {
		idxdoc_destroy(idx, doc);
	}
#if 0 // FIXME
	while ((token = TAILQ_PREV(token, &tokens->list, entry)) != NULL) {
		idxterm_decr_total(idx, idxterm, token->count);
	}
#endif
	return -1;
}

int
idx_dtmap_sync(nxs_index_t *idx)
{
	idxmap_t *idxmap = &idx->dt_memmap;
	size_t seen_data_len, target_len, consumed_len = 0;;
	idxdt_hdr_t *hdr;
	void *dataptr;
	mmrw_t mm;
	int ret = -1;

	/*
	 * Fetch the data length.  Compute the length of data to consume.
	 */

	hdr = idxmap->baseptr;
	ASSERT(idxmap->fd > 0 && hdr != NULL);

	seen_data_len = be64toh(atomic_load_acquire(&hdr->data_len));
	if (seen_data_len == idx->dt_consumed) {
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
	hdr = idx_db_map(idxmap, sizeof(idxdt_hdr_t) + seen_data_len, false);
	if (hdr == NULL) {
		return -1;
	}
	target_len = seen_data_len - idx->dt_consumed;
	dataptr = IDXDT_DATA_PTR(hdr, idx->dt_consumed);
	app_dbgx("current %zu, consuming %zu", target_len, idx->dt_consumed);

	/*
	 * Fetch the document mappings.
	 */
	mmrw_init(&mm, dataptr, target_len);
	while (mm.remaining) {
		nxs_doc_id_t doc_id;
		uint32_t n, doc_total_len;
		uint64_t offset;

		offset = (uintptr_t)mm.curptr - (uintptr_t)hdr;

		if (mmrw_fetch64(&mm, &doc_id) == -1 ||
		    mmrw_fetch32(&mm, &doc_total_len) == -1 ||
		    mmrw_fetch32(&mm, &n) == -1) {
			goto err;
		}
		if (!idxdoc_create(idx, doc_id, offset)) {
			goto err;
		}

		/*
		 * Build the reverse term-document index.
		 */
		for (unsigned i = 0; i < n; i++) {
			nxs_term_id_t id;
			uint32_t count;

			if (mmrw_fetch32(&mm, &id) == -1 ||
			    mmrw_fetch32(&mm, &count) == -1) {
				goto err;  // XXX: revert additions
			}
			if (idxterm_add_doc(idx, id, doc_id) == -1) {
				goto err;  // XXX: revert additions
			}
		}
		consumed_len += IDXDT_META_LEN(n);
	}
	ASSERT(consumed_len == target_len);
	ret = 0;
err:
	idx->dt_consumed = consumed_len;
	app_dbgx("consumed = %zu", consumed_len);
	return ret;
}

/*
 * idx_get_token_count: get the total token count in the index.
 */
uint64_t
idx_get_token_count(const nxs_index_t *idx)
{
	const idxmap_t *idxmap = &idx->dt_memmap;
	const idxdt_hdr_t *hdr = idxmap->baseptr;
	return IDXDT_TOKEN_COUNT(hdr);
}

/*
 * idx_get_doc_count: get the total document count in the index.
 */
uint32_t
idx_get_doc_count(const nxs_index_t *idx)
{
	const idxmap_t *idxmap = &idx->dt_memmap;
	const idxdt_hdr_t *hdr = idxmap->baseptr;
	return IDXDT_DOC_COUNT(hdr);
}
