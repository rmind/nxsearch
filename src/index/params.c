/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdlib.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#include <yyjson.h>
#pragma GCC diagnostic pop

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "utils.h"

typedef struct {
	void *		next;
	void *		ptr;
} param_memref_t;

struct nxs_params {
	yyjson_mut_doc *	doc;
	yyjson_mut_val *	root;
	param_memref_t *	memrefs;
};

__dso_public nxs_params_t *
nxs_params_create(void)
{
	nxs_params_t *params;

	if ((params = calloc(1, sizeof(nxs_params_t))) == NULL) {
		return NULL;
	}
	params->doc = yyjson_mut_doc_new(NULL);
	params->root = yyjson_mut_obj(params->doc);
	yyjson_mut_doc_set_root(params->doc, params->root);
	return params;
}

static char *
memref_strdup(nxs_params_t *params, const char *s)
{
	param_memref_t *mr;

	if ((mr = calloc(1, sizeof(param_memref_t))) == NULL) {
		return NULL;
	}
	if ((mr->ptr = strdup(s)) == NULL) {
		free(mr);
		return NULL;
	}
	mr->next = params->memrefs;
	params->memrefs = mr;
	return mr->ptr;
}

__dso_public int
nxs_params_set_str(nxs_params_t *params, const char *key, const char *val)
{
	const char *ckey = memref_strdup(params, key);
	const char *cval = memref_strdup(params, val);

	if (ckey == NULL || cval == NULL || !yyjson_mut_obj_add_str(
	    params->doc, params->root, ckey, cval)) {
		return -1;
	}
	return 0;
}

__dso_public int
nxs_params_set_uint(nxs_params_t *params, const char *key, uint64_t val)
{
	const char *ckey = memref_strdup(params, key);

	if (ckey == NULL || !yyjson_mut_obj_add_uint(
	    params->doc, params->root, ckey, val)) {
		return -1;
	}
	return 0;
}

/*
 * nxs_params_tojson: return the parameters as a JSON string.
 *
 * => The string must be released with free(3) by the caller.
 */
char *
nxs_params_tojson(nxs_params_t *params, size_t *len)
{
	return yyjson_mut_write(params->doc, 0, len);
}

__dso_public void
nxs_params_release(nxs_params_t *params)
{
	param_memref_t *mr = params->memrefs;

	while (mr) {
		param_memref_t *next = mr->next;
		free(mr->ptr);
		free(mr);
		mr = next;
	}
	if (params->doc) {
		yyjson_mut_doc_free(params->doc);
	}
	free(params);
}
