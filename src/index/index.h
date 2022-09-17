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

#include <roaring/roaring.h>

#include "nxs.h"
#include "tokenizer.h"
#include "rhashmap.h"

#define	IDX_SIZE_STEP		(32UL * 1024)	// 32 KB

typedef struct idxterm {
	nxs_term_id_t		id;
	uint32_t		offset;
	TAILQ_ENTRY(idxterm)	entry;
	roaring_bitmap_t *	doc_bitmap;
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
} idxmap_t;

struct nxs_index {
	/*
	 * Terms list.
	 */
	idxmap_t		terms_memmap;
	size_t			terms_consumed;
	nxs_term_id_t		terms_last_id;

	rhashmap_t *		term_map;
	TAILQ_HEAD(, idxterm)	term_list;

	/*
	 * Document-term index.
	 */
	idxmap_t		dt_memmap;
	size_t			dt_consumed;
	rhashmap_t *		dt_map;
	TAILQ_HEAD(, idxdoc)	dt_list;

	/*
	 * Term-document map (the reverse index).
	 */
	rhashmap_t *		td_map;
	filter_pipeline_t *	fp;

	/* Index name. */
	char *			name;
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

idxterm_t *	idxterm_create(nxs_index_t *, const char *,
		    const size_t, const size_t);
void		idxterm_destroy(nxs_index_t *, idxterm_t *);

void		idxterm_resolve_tokens(nxs_index_t *, tokenset_t *, bool);
void		idxterm_assign(nxs_index_t *, idxterm_t *, nxs_term_id_t);
idxterm_t *	idxterm_lookup(nxs_index_t *, const char *, size_t);
void		idxterm_incr_total(nxs_index_t *, const idxterm_t *, unsigned);
int		idxterm_add_doc(nxs_index_t *, nxs_term_id_t, nxs_doc_id_t);

/*
 * Document (in-memory) interface.
 */
idxdoc_t *	idxdoc_create(nxs_index_t *, nxs_doc_id_t, uint64_t);
void		idxdoc_destroy(nxs_index_t *, idxdoc_t *);
idxdoc_t *	idxdoc_lookup(nxs_index_t *, nxs_doc_id_t);
int		idxdoc_get_termcount(const nxs_index_t *,
		    const idxdoc_t *, nxs_term_id_t);
unsigned	idxdoc_get_totalcount(const nxs_index_t *);

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
int		idx_dtmap_open(nxs_index_t *, const char *);
int		idx_dtmap_add(nxs_index_t *, nxs_doc_id_t, tokenset_t *);
int		idx_dtmap_sync(nxs_index_t *);
void		idx_dtmap_close(nxs_index_t *);

#endif
