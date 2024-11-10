/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef	_NXS_INDEX_H_
#define	_NXS_INDEX_H_

#include <sys/queue.h>
#include <inttypes.h>
#include <stdbool.h>

#include <roaring/roaring64.h>

#include "nxs.h"
#include "tokenizer.h"
#include "rhashmap.h"
#include "deque.h"
#include "levdist.h"
#include "bktree.h"

#define	IDX_SIZE_STEP		(32UL * 1024)	// 32 KB

#define	LEVDIST_TOLERANCE	(2)

typedef uint32_t nxs_term_id_t;

typedef enum {
	TF_IDF		= 0,
	BM25		= 1,
	INVALID_ALGO	= -1,
} ranking_algo_t;

/*
 * idxterm_t and idxdoc_t are the in-memory structures mapping their
 * corresponding term and document IDs to the relevant offset in their
 * memory-mapped index.
 */

typedef struct idxterm {
	nxs_term_id_t		id;
	uint32_t		offset;
	TAILQ_ENTRY(idxterm)	entry;

	/* Bitmap of the documents in which this term occurs. */
	roaring64_bitmap_t *	doc_bitmap;
	uint16_t		value_len;
	char			value[];
} idxterm_t;

typedef struct idxdoc {
	nxs_doc_id_t		id;
	uint64_t		offset;
	TAILQ_ENTRY(idxdoc)	entry;
} idxdoc_t;

typedef struct idxmap {
	int			fd;
	void *			baseptr;
	size_t			mapped_len;
	bool			sync;
} idxmap_t;

struct nxs_index {
	/*
	 * Terms list.
	 */
	idxmap_t		terms_memmap;
	size_t			terms_consumed;
	nxs_term_id_t		terms_last_id;

	rhashmap_t *		term_map;
	bktree_t *		term_bkt;
	TAILQ_HEAD(, idxterm)	term_list;
	size_t			term_count;
	levdist_t *		term_levctx;

	/*
	 * Document-term index.
	 */
	idxmap_t		dt_memmap;
	size_t			dt_consumed;
	rhashmap_t *		dt_map;
	TAILQ_HEAD(, idxdoc)	dt_list;
	size_t			dt_count;

	/*
	 * Term-document map (the reverse index).
	 */
	rhashmap_t *		td_map;
	filter_pipeline_t *	fp;
	ranking_algo_t		algo;

	/* Instance back-pointer, params, index name, list entry. */
	nxs_t *			nxs;
	nxs_params_t *		params;
	char *			name;
	TAILQ_ENTRY(nxs_index)	entry;
};

/*
 * Generic on-disk index interface.
 */
int		idx_db_open(idxmap_t *, const char *, bool *);
void *		idx_db_map(idxmap_t *, size_t, bool);
void		idx_db_release(idxmap_t *);

/*
 * Term (in-memory) interface.
 */
int		idxterm_sysinit(nxs_index_t *);
void		idxterm_sysfini(nxs_index_t *);

idxterm_t *	idxterm_create(const char *, const size_t, const size_t);
void		idxterm_destroy(nxs_index_t *, idxterm_t *);

idxterm_t *	idxterm_insert(nxs_index_t *, idxterm_t *, nxs_term_id_t);
idxterm_t *	idxterm_lookup(nxs_index_t *, const char *, size_t);
idxterm_t *	idxterm_lookup_by_id(nxs_index_t *, nxs_term_id_t);
idxterm_t *	idxterm_fuzzysearch(nxs_index_t *, const char *, size_t);
int		idxterm_add_doc(idxterm_t *, nxs_doc_id_t);
void		idxterm_del_doc(idxterm_t *, nxs_doc_id_t);
void		idxterm_incr_total(nxs_index_t *, const idxterm_t *, unsigned);
void		idxterm_decr_total(nxs_index_t *, const idxterm_t *, unsigned);
uint64_t	idxterm_get_total(nxs_index_t *, const idxterm_t *);

/*
 * Document (in-memory) interface.
 */
idxdoc_t *	idxdoc_create(nxs_index_t *, nxs_doc_id_t, uint64_t);
void		idxdoc_destroy(nxs_index_t *, idxdoc_t *);
idxdoc_t *	idxdoc_lookup(nxs_index_t *, nxs_doc_id_t);

int		idxdoc_get_doclen(const nxs_index_t *, const idxdoc_t *);
int		idxdoc_get_termcount(const nxs_index_t *,
		    const idxdoc_t *, nxs_term_id_t);

/*
 * Terms index interface.
 */
int		idx_terms_open(nxs_index_t *, const char *);
int		idx_terms_add(nxs_index_t *, tokenset_t *);
int		idx_terms_sync(nxs_index_t *);
void		idx_terms_close(nxs_index_t *);

/*
 * Document-terms index interface.
 */

#define	DTMAP_PARTIAL_SYNC	(0x01)

int		idx_dtmap_open(nxs_index_t *, const char *);
int		idx_dtmap_add(nxs_index_t *, nxs_doc_id_t, tokenset_t *);
int		idx_dtmap_remove(nxs_index_t *, nxs_doc_id_t);
int		idx_dtmap_sync(nxs_index_t *, unsigned);
void		idx_dtmap_close(nxs_index_t *);

uint64_t	idx_get_token_count(const nxs_index_t *);
uint32_t	idx_get_doc_count(const nxs_index_t *);

#endif
