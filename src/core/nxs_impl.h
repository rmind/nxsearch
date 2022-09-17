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

int	nxs_filter_register(nxs_t *, const char *, const filter_ops_t *);

float	tf_idf(const nxs_index_t *, const idxterm_t *, const idxdoc_t *);

#endif
