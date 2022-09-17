/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _NXSLIB_H_
#define _NXSLIB_H_

#include <inttypes.h>

/*
 * General nxsearch library API.
 */

typedef uint32_t nxs_term_id_t;
typedef uint64_t nxs_doc_id_t;

struct nxs;
typedef struct nxs nxs_t;

nxs_t *		nxs_create(const char *);
void		nxs_destroy(nxs_t *);

/*
 * Index handling API.
 */

struct nxs_index;
typedef struct nxs_index nxs_index_t;

nxs_index_t *	nxs_index_create(nxs_t *, const char *);
nxs_index_t *	nxs_index_open(nxs_t *, const char *);
void		nxs_index_close(nxs_t *, nxs_index_t *);
int		nxs_index_add(nxs_index_t *, uint64_t, const char *, size_t);

/*
 * Search API.
 */

typedef struct nxs_result_entry {
	nxs_doc_id_t	doc_id;
	float		score;
	void *		next;
} nxs_result_entry_t;

typedef struct nxs_results {
	unsigned	count;
	nxs_result_entry_t *entries;
} nxs_results_t;

nxs_results_t *	nxs_index_search(nxs_index_t *, const char *, size_t);
void		nxs_results_release(nxs_results_t *);

#endif
