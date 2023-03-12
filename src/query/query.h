/*
 * Copyright (c) 2023 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _QUERY_H_
#define _QUERY_H_

struct query;
typedef struct query query_t;

#ifdef __NXS_PARSER_PRIVATE

struct query {
	nxs_index_t *	idx;

	expr_t *	root;

	/* Tokenset to be resolved. */
	tokenset_t *	tokens;
};

#endif

query_t *	query_create(nxs_index_t *);
void		query_destroy(query_t *);
int		query_prepare(query_t *, unsigned);

#endif
