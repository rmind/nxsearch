/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _NXS_IMPL_H_
#define _NXS_IMPL_H_

#if !defined(__NXSLIB_PRIVATE)
#error "only to be used by internal source code"
#endif

#include <inttypes.h>
#include "nxs.h"
#include "filters.h"
#include "index.h"
#include "rhashmap.h"

struct nxs {
	char *			basedir;
	unsigned		filters_count;
	filter_entry_t *	filters;
	rhashmap_t *		indexes;
};

int		nxs_filter_register(nxs_t *, const char *, const filter_ops_t *);
fts_index_t *	nxs_index_open(nxs_t *, const char *);
void		nxs_index_close(nxs_t *, fts_index_t *);

#endif
