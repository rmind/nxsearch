/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _NXSLIB_H_
#define _NXSLIB_H_

#include <sys/cdefs.h>
#include <inttypes.h>
#include <stdbool.h>

__BEGIN_DECLS

/*
 * General nxsearch library API.
 */

typedef uint32_t nxs_term_id_t;
typedef uint64_t nxs_doc_id_t;

struct nxs;
typedef struct nxs nxs_t;

nxs_t *		nxs_create(const char *);
void		nxs_destroy(nxs_t *);
const char *	nxs_get_error(const nxs_t *);

/*
 * Parameters API.
 */

struct nxs_params;
typedef struct nxs_params nxs_params_t;

nxs_params_t *	nxs_params_create(void);
nxs_params_t *	nxs_params_fromjson(nxs_t *, const char *, size_t);

int		nxs_params_set_strset(nxs_params_t *, const char *,
		    const char **, size_t);
int		nxs_params_set_str(nxs_params_t *, const char *, const char *);
int		nxs_params_set_uint(nxs_params_t *, const char *, uint64_t);
void		nxs_params_release(nxs_params_t *);

/*
 * Index handling API.
 */

struct nxs_index;
typedef struct nxs_index nxs_index_t;

nxs_index_t *	nxs_index_create(nxs_t *, const char *, nxs_params_t *);
int		nxs_index_destroy(nxs_t *, const char *);

nxs_index_t *	nxs_index_open(nxs_t *, const char *);
void		nxs_index_close(nxs_index_t *);
int		nxs_index_add(nxs_index_t *, nxs_term_id_t,
		    const char *, size_t);
int		nxs_index_remove(nxs_index_t *, nxs_term_id_t);

/*
 * Query and response API.
 */

struct nxs_resp;
typedef struct nxs_resp nxs_resp_t;

nxs_resp_t *	nxs_index_search(nxs_index_t *, const char *, size_t);

void		nxs_resp_iter_reset(nxs_resp_t *);
bool		nxs_resp_iter_result(nxs_resp_t *, nxs_doc_id_t *, float *);
unsigned	nxs_resp_resultcount(const nxs_resp_t *);

const char *	nxs_resp_geterror(const nxs_resp_t *);
char *		nxs_resp_tojson(nxs_resp_t *, size_t *);
void		nxs_resp_release(nxs_resp_t *);

__END_DECLS

#endif
