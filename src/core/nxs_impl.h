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

typedef struct filter_entry filter_entry_t;

struct nxs {
	/* Base directory and the error message with code. */
	char *			basedir;
	char *			errmsg;
	nxs_err_t		errcode;

	/* Opened index map and list. */
	rhashmap_t *		indexes;
	TAILQ_HEAD(, nxs_index)	index_list;

	/* Filter list. */
	TAILQ_HEAD(, filter_entry) filter_list;
	unsigned		filters_count;
	filter_entry_t *	filters;
};

#define	NXS_DEFAULT_RESULTS_LIMIT	1000
#define	NXS_DEFAULT_RANKING_ALGO	"BM25"
#define	NXS_DEFAULT_LANGUAGE		"en"

int	nxs_filter_register(nxs_t *, const char *, const filter_ops_t *, void *);

void	nxs_clear_error(nxs_t *);
void	nxs_error_checkpoint(nxs_t *);

/*
 * Ranking algorithms.
 */

typedef float (*ranking_func_t)(const nxs_index_t *,
    const idxterm_t *, const idxdoc_t *);

float	tf_idf(const nxs_index_t *, const idxterm_t *, const idxdoc_t *);
float	bm25(const nxs_index_t *, const idxterm_t *, const idxdoc_t *);

ranking_algo_t	get_ranking_func_id(const char *);
ranking_func_t	get_ranking_func(ranking_algo_t);

/*
 * Internal params and response API.
 */

int		nxs_params_serialize(nxs_t *, const nxs_params_t *, const char *);
nxs_params_t *	nxs_params_unserialize(nxs_t *, const char *);
const char **	nxs_params_get_strlist(nxs_params_t *, const char *, size_t *);
const char *	nxs_params_get_str(nxs_params_t *, const char *);
int		nxs_params_get_uint(nxs_params_t *, const char *, uint64_t *);
int		nxs_params_get_bool(nxs_params_t *, const char *, bool *);

nxs_resp_t *	nxs_resp_create(size_t);
int		nxs_resp_addresult(nxs_resp_t *, const idxdoc_t *, float);
void		nxs_resp_adderror(nxs_resp_t *, nxs_err_t, const char *);
void		nxs_resp_build(nxs_resp_t *);

/*
 * Error messaging.
 */

void		_nxs_decl_err(nxs_t *, int, const char *, int,
		    const char *, nxs_err_t, const char *, ...);

#define	nxs_decl_errx(nxs, code, msg, ...) \
    _nxs_decl_err((nxs), LOG_ERR, \
    __FILE__, __LINE__, __func__, (code), (msg), __VA_ARGS__)

#define	nxs_decl_err(nxs, code, msg, ...) \
    _nxs_decl_err((nxs), LOG_ERR|LOG_EMSG, \
    __FILE__, __LINE__, __func__, (code), (msg), __VA_ARGS__)

#endif
