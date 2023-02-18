/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Document-term index.
 *
 * This module manages the document-term index (dtmap).  It is generally
 * an append only structure which follows the general idxmap synchronization
 * logic.  The index contains the mappings of document IDs to the set of
 * term IDs and their occurrence counters.
 *
 * See the storage.h header for more details on the on-disk layout.
 *
 * Additional synchronization notes
 *
 *	On addition and removal of the document, the term index sync must
 *	precede the document-term index sync.  The term index must be synced
 *	with the dtmap lock held, because new terms and documents can be
 *	added between the two syncs and the newly consumed documents would
 *	reference them (therefore, dtmap_insert() would fail as it could not
 *	find the references terms).  Acquiring the dtmap lock for both syncs
 *	prevents this race condition.
 *
 * Document deletion
 *
 *	The document deletion is implemented by: 1) clearing the document
 *	ID of its current record block by atomically setting it to zero;
 *	2) adding a new record block for the document ID being deleted with
 *	the document length being set to zero.
 *
 *	The former ensures that fresh opening of the index will skip the
 *	records for the deleted documents.  The latter will notify active
 *	index references to remove the document from the in-memory structure.
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
idx_dtmap_verify(nxs_index_t *idx, const idxmap_t *idxmap)
{
	const idxdt_hdr_t *hdr = idxmap->baseptr;

	if (memcmp(hdr->mark, NXS_D_MARK, sizeof(hdr->mark)) != 0) {
		nxs_decl_err(idx->nxs, NXS_ERR_FATAL,
		    "corrupted dtmap index header", NULL);
		return -1;
	}
	if (hdr->ver != NXS_ABI_VER) {
		nxs_decl_err(idx->nxs, NXS_ERR_FATAL,
		    "incompatible nxsearch index version", NULL);
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
		nxs_decl_err(idx->nxs, NXS_ERR_SYSTEM,
		    "could not open dtmap index", NULL);
		return -1;
	}

	/*
	 * Map and, if creating, initialize the header.
	 */
	baseptr = idx_db_map(&idx->dt_memmap, IDX_SIZE_STEP, false);
	if (baseptr == NULL) {
		nxs_decl_err(idx->nxs, NXS_ERR_SYSTEM,
		    "dtmap mapping failed", NULL);
		goto err;
	}
	if (created && idx_dtmap_init(&idx->dt_memmap) == -1) {
		goto err;
	}
	if (!created && idx_dtmap_verify(idx, &idx->dt_memmap) == -1) {
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
	f_lock_exit(fd);

	/*
	 * Finally, load the map.
	 */
	return idx_dtmap_sync(idx, DTMAP_PARTIAL_SYNC);
err:
	f_lock_exit(fd);
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

/*
 * dtmap_termblock_cmp: comparator for the term blocks (containing
 * the term ID and counter tuple), based on term ID value.
 */
static int
dtmap_termblock_cmp(const void * restrict b1, const void * restrict b2)
{
	ASSERT(ALIGNED_POINTER(b1, uint32_t) && ALIGNED_POINTER(b2, uint32_t));

	const nxs_term_id_t b1_term_id = be32toh(*(const uint32_t *)b1);
	const nxs_term_id_t b2_term_id = be32toh(*(const uint32_t *)b2);

	if (b1_term_id < b2_term_id)
		return -1;
	if (b1_term_id > b2_term_id)
		return 1;
	return 0;
}

static void
dtmap_decr_totals(nxs_index_t *idx, tokenset_t *tokens, token_t *target)
{
	token_t *it;

	TAILQ_FOREACH(it, &tokens->list, entry) {
		if (it == target) {
			break;
		}
		idxterm_decr_total(idx, it->idxterm, it->count);
	}
}

static void *
dtmap_build_block(nxs_index_t *idx, nxs_doc_id_t doc_id, tokenset_t *tokens)
{
	void *data, *term_blocks;
	token_t *token = NULL;
	size_t block_len;
	mmrw_t mm;

	block_len = IDXDT_META_LEN(tokens->count);
	if ((data = malloc(block_len)) == NULL) {
		return NULL;
	}
	mmrw_init(&mm, data, block_len);

	/* Fill the document metadata. */
	mmrw_store64(&mm, doc_id);
	mmrw_store32(&mm, tokens->seen);
	mmrw_store32(&mm, tokens->count);

	/*
	 * Fill the terms seen in the document.
	 */
	term_blocks = MAP_GET_OFF(data, 8 + 4 + 4);
	TAILQ_FOREACH(token, &tokens->list, entry) {
		idxterm_t *idxterm = token->idxterm;

		/* The term must be resolved. */
		ASSERT(idxterm != NULL);
		ASSERT(idxterm->id > 0);

		mmrw_store32(&mm, idxterm->id);
		mmrw_store32(&mm, token->count);

		if (idxterm_add_doc(idxterm, doc_id) == -1) {
			/* Revert the increments. */
			dtmap_decr_totals(idx, tokens, token);
			nxs_decl_err(idx->nxs, NXS_ERR_FATAL,
			    "idxterm_add_doc failed", NULL);
			return NULL;
		}
		idxterm_incr_total(idx, idxterm, token->count);
	}

	/* Sort them by term IDs. */
	qsort(term_blocks, tokens->count,
	    sizeof(uint32_t) * 2, dtmap_termblock_cmp);

	return data;
}

int
idx_dtmap_add(nxs_index_t *idx, nxs_doc_id_t doc_id, tokenset_t *tokens)
{
	idxmap_t *idxmap = &idx->dt_memmap;
	size_t append_len, data_len, target_len, offset;
	void *dataptr, *block = NULL;
	idxdoc_t *doc = NULL;
	idxdt_hdr_t *hdr;
	int ret = -1;

	ASSERT(doc_id > 0);
	ASSERT(!TAILQ_EMPTY(&tokens->list));
	ASSERT(TAILQ_EMPTY(&tokens->staging));

	app_dbgx("processing %u tokens", tokens->count);
	ASSERT(tokens->count > 0);

	/*
	 * Build the document-term block.
	 */
	if ((block = dtmap_build_block(idx, doc_id, tokens)) == NULL) {
		return -1;
	}

	/*
	 * First, pre-sync without the lock as an optimization.
	 * Lock the file, sync both indexes and remap if necessary.
	 */
	if (idx_dtmap_sync(idx, DTMAP_PARTIAL_SYNC) == -1 ||
	    f_lock_enter(idxmap->fd, LOCK_EX) == -1) {
		free(block);
		return -1;
	}
again:
	hdr = idxmap->baseptr;
	ASSERT(idxmap->fd > 0 && hdr != NULL);
	data_len = be64toh(atomic_load_acquire(&hdr->data_len));
	if (idx->dt_consumed < data_len) {
		/*
		 * Sync the term index and then the document-term index.
		 * The dtmap lock must be held while syncing the terms.
		 */
		if (idx_terms_sync(idx) == -1 ||
		    idx_dtmap_sync(idx, 0) == -1) {
			goto err;
		}
		goto again;
	}
	if (idxdoc_lookup(idx, doc_id)) {
		/* Race condition: document ID is indexed already. */
		nxs_decl_errx(idx->nxs, NXS_ERR_EXISTS,
		    "document %"PRIu64" is already indexed", doc_id);
		goto err;
	}

	/*
	 * Compute the target length and extend if necessary.
	 */
	append_len = IDXDT_META_LEN(tokens->count);
	target_len = sizeof(idxdt_hdr_t) + data_len + append_len;
	if ((hdr = idx_db_map(idxmap, target_len, true)) == NULL) {
		nxs_decl_err(idx->nxs, NXS_ERR_SYSTEM,
		    "dtmap mapping failed", NULL);
		goto err;
	}

	/*
	 * Add the document to the in-memory map.
	 */
	offset = sizeof(idxdt_hdr_t) + data_len;
	if ((doc = idxdoc_create(idx, doc_id, offset)) == NULL) {
		nxs_decl_err(idx->nxs, NXS_ERR_SYSTEM,
		    "idxdoc_create failed", NULL);
		goto err;
	}
	ASSERT(ALIGNED_POINTER(offset, nxs_doc_id_t));

	/*
	 * Produce the document-term block.
	 */
	dataptr = IDXDT_DATA_PTR(hdr, data_len);
	memcpy(dataptr, block, append_len);

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

	doc = NULL;
	ret = 0;
err:
	f_lock_exit(idxmap->fd);
	if (doc) {
		idxdoc_destroy(idx, doc);
	}
	if (block && ret) {
		dtmap_decr_totals(idx, tokens, NULL);
	}
	free(block);
	return ret;
}

static bool
dtmap_deletion(nxs_index_t *idx, nxs_doc_id_t doc_id, uint32_t doc_total_len)
{
	/*
	 * If the document ID is zero, then this document was
	 * deleted.  Just skip the whole block.
	 */
	if (doc_id == 0) {
		app_dbgx("doc %"PRIu64 " deleted, skipping", doc_id);
		return true;
	}

	/*
	 * If the document length is zero, then it is mark that
	 * this document ID was deleted.  Check the in-memory
	 * structure and remove it if still present.
	 */
	if (doc_total_len == 0) {
		idxdoc_t *doc = idxdoc_lookup(idx, doc_id);
		if (doc) {
			app_dbgx("doc %"PRIu64 " deleted, cleanup", doc_id);
			idxdoc_destroy(idx, doc);
		}
		return true;
	}

	return false;
}

static int
dtmap_build_tdmap(nxs_index_t *idx, const nxs_doc_id_t doc_id,
    mmrw_t *mm, const unsigned n)
{
	const uintptr_t tdmap_offset = MMRW_GET_OFFSET(mm);
	nxs_term_id_t term_id;
	idxterm_t *term;
	uint32_t count;
	unsigned i;

	/*
	 * Build the reverse term-document index.
	 */
	for (i = 0; i < n; i++) {
		if (mmrw_fetch32(mm, &term_id) == -1 ||
		    mmrw_fetch32(mm, &count) == -1) {
			nxs_decl_errx(idx->nxs, NXS_ERR_FATAL,
			    "corrupted dtmap index", NULL);
			goto err;
		}
		if ((term = idxterm_lookup_by_id(idx, term_id)) == NULL) {
			nxs_decl_errx(idx->nxs, NXS_ERR_FATAL,
			    "idxterm_lookup_by_id on term %u failed", term_id);
			goto err;
		}
		if (idxterm_add_doc(term, doc_id) == -1) {
			nxs_decl_err(idx->nxs, NXS_ERR_FATAL,
			    "idxterm_add_doc failed", NULL);
			goto err;
		}
	}
	return 0;
err:
	/*
	 * Revert the index changes.
	 */
	mmrw_seek(mm, tdmap_offset);
	while (i--) {
		if (mmrw_fetch32(mm, &term_id) == -1 ||
		    mmrw_fetch32(mm, &count) == -1) {
			nxs_decl_errx(idx->nxs, NXS_ERR_FATAL,
			    "corrupted dtmap index", NULL);
			return -1;
		}
		term = idxterm_lookup_by_id(idx, term_id);
		ASSERT(term != NULL);
		idxterm_del_doc(term, doc_id);
	}
	return -1;
}

int
idx_dtmap_sync(nxs_index_t *idx, unsigned flags)
{
	idxmap_t *idxmap = &idx->dt_memmap;
	size_t seen_data_len, target_len, consumed_len = 0;
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
	ASSERT(idx->dt_consumed < seen_data_len);

	/*
	 * Ensure mapping: verify that it does not exceed the mapping
	 * length or re-maps using the new length.
	 *
	 * We will consume from the last processed data offset.
	 */
	hdr = idx_db_map(idxmap, sizeof(idxdt_hdr_t) + seen_data_len, false);
	if (hdr == NULL) {
		nxs_decl_err(idx->nxs, NXS_ERR_SYSTEM,
		    "dtmap mapping failed", NULL);
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
		idxdoc_t *doc;

		offset = (uintptr_t)mm.curptr - (uintptr_t)hdr;
		ASSERT(ALIGNED_POINTER(offset, nxs_doc_id_t));

		if (mmrw_fetch64(&mm, &doc_id) == -1 ||
		    mmrw_fetch32(&mm, &doc_total_len) == -1 ||
		    mmrw_fetch32(&mm, &n) == -1) {
			nxs_decl_errx(idx->nxs, NXS_ERR_FATAL,
			    "corrupted dtmap index", NULL);
			goto err;
		}

		/*
		 * Check and handle the document deletion marks.
		 * If deleted or marked block, then just advance.
		 */
		if (dtmap_deletion(idx, doc_id, doc_total_len)) {
			ASSERT(doc_total_len || n == 0);

			if (mmrw_advance(&mm, n * (4 + 4)) == -1) {
				goto err;
			}
			consumed_len += IDXDT_META_LEN(n);
			continue;
		}

		/*
		 * Create the document and build the reverse
		 * term-document index.
		 */
		if ((doc = idxdoc_create(idx, doc_id, offset)) == NULL) {
			nxs_decl_err(idx->nxs, NXS_ERR_SYSTEM,
			    "idxdoc_create failed", NULL);
			goto err;
		}
		if (dtmap_build_tdmap(idx, doc_id, &mm, n) == -1) {
			idxdoc_destroy(idx, doc);
			if (flags & DTMAP_PARTIAL_SYNC) {
				/* No error if partial sync is allowed. */
				ret = 0;
				goto err;
			}
			goto err;
		}
		consumed_len += IDXDT_META_LEN(n);
	}
	ASSERT(consumed_len == target_len);
	ret = 0;
err:
	idx->dt_consumed += consumed_len;
	app_dbgx("consumed = %zu", consumed_len);
	return ret;
}

int
idx_dtmap_remove(nxs_index_t *idx, nxs_doc_id_t doc_id)
{
	idxmap_t *idxmap = &idx->dt_memmap;
	size_t append_len, data_len, target_len;
	uint64_t *doc_id_ptr;
	idxdt_hdr_t *hdr;
	idxdoc_t *doc;
	uint32_t seen;
	unsigned n;
	int ret = -1;
	mmrw_t mm;

	/*
	 * Sync the term index and then the document-term index.
	 * WARNING: The dtmap lock must be held while syncing the terms.
	 */
	if (f_lock_enter(idxmap->fd, LOCK_EX) == -1) {
		return -1;
	}
	if (idx_terms_sync(idx) == -1 || idx_dtmap_sync(idx, 0) == -1) {
		goto out;
	}

	if ((doc = idxdoc_lookup(idx, doc_id)) == NULL) {
		nxs_decl_err(idx->nxs, NXS_ERR_MISSING,
		    "document %"PRIu64 "not found", doc_id);
		goto out;
	}

	hdr = idxmap->baseptr;
	ASSERT(idxmap->fd > 0 && hdr != NULL);

	/*
	 * We will be adding a special marker (two 64-bit integers),
	 * a document ID with a zero length, to indicate deletion.
	 */
	append_len = 8 + 8;
	data_len = be64toh(atomic_load_acquire(&hdr->data_len));
	target_len = sizeof(idxdt_hdr_t) + data_len + append_len;
	if ((hdr = idx_db_map(idxmap, target_len, true)) == NULL) {
		nxs_decl_err(idx->nxs, NXS_ERR_SYSTEM,
		    "dtmap mapping failed", NULL);
		goto out;
	}

	/*
	 * Find the document in the index.
	 */
	doc_id_ptr = MAP_GET_OFF(idxmap->baseptr, doc->offset);
	mmrw_init(&mm, doc_id_ptr,
	    (sizeof(idxdt_hdr_t) + idx->dt_consumed) - doc->offset);

	/*
	 * Set the document ID to zero.  This indicates to the fresh
	 * consumers that this document entry is no longer valid.
	 */
	atomic_store_release(doc_id_ptr, 0);

	/*
	 * Iterate the document terms and decrement the term counters.
	 */
	if (mmrw_advance(&mm, 8) == -1 ||
	    mmrw_fetch32(&mm, &seen) == -1 ||
	    mmrw_fetch32(&mm, &n) == -1) {
		return -1;
	}
	for (unsigned i = 0; i < n; i++) {
		nxs_term_id_t term_id;
		idxterm_t *term;
		uint32_t count;

		if (mmrw_fetch32(&mm, &term_id) == -1 ||
		    mmrw_fetch32(&mm, &count) == -1) {
			goto out;
		}
		if ((term = idxterm_lookup_by_id(idx, term_id)) == NULL) {
			goto out;
		}
		idxterm_del_doc(term, doc->id);
		idxterm_decr_total(idx, term, count);
	}

	/*
	 * Append the index with a special entry: document ID with document
	 * length and term count being set to zero.  This indicates to the
	 * active consumers that the document has been removed.
	 *
	 * Finally, remove the in-memory document entry.
	 */
	mmrw_init(&mm, IDXDT_DATA_PTR(hdr, data_len), append_len);
	if (mmrw_store64(&mm, doc_id) == -1 || mmrw_store64(&mm, 0) == -1) {
		goto out;
	}
	idxdoc_destroy(idx, doc);

	/* Decrement the counters. */
	atomic_store_relaxed(&hdr->doc_count,
	    htobe32(IDXDT_DOC_COUNT(hdr) - 1));
	atomic_store_relaxed(&hdr->token_count,
	    htobe64(IDXDT_TOKEN_COUNT(hdr) - seen));

	/* Publish the new data length. */
	idx->dt_consumed = data_len + append_len;
	atomic_store_release(&hdr->data_len, htobe64(idx->dt_consumed));
	ret = 0;
out:
	f_lock_exit(idxmap->fd);
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
