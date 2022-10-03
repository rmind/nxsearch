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

struct nxs_params {
	yyjson_mut_doc *	doc;
	yyjson_mut_val *	root;
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

__dso_public int
nxs_params_set_strset(nxs_params_t *params, const char *key,
    const char **vals, size_t count)
{
	yyjson_mut_val *ckey = yyjson_mut_strcpy(params->doc, key);
	yyjson_mut_val *arr;

	arr = yyjson_mut_arr_with_strcpy(params->doc, vals, count);
	if (!yyjson_mut_obj_add(params->root, ckey, arr)) {
		return -1;
	}
	return 0;
}

__dso_public int
nxs_params_set_str(nxs_params_t *params, const char *key, const char *val)
{
	yyjson_mut_val *ckey = yyjson_mut_strcpy(params->doc, key);
	yyjson_mut_val *cval = yyjson_mut_strcpy(params->doc, val);

	if (!yyjson_mut_obj_add(params->root, ckey, cval)) {
		return -1;
	}
	return 0;
}

__dso_public int
nxs_params_set_uint(nxs_params_t *params, const char *key, uint64_t val)
{
	yyjson_mut_val *ckey = yyjson_mut_strcpy(params->doc, key);
	yyjson_mut_val *cval = yyjson_mut_uint(params->doc, val);

	if (!yyjson_mut_obj_add(params->root, ckey, cval)) {
		return -1;
	}
	return 0;
}

__dso_public void
nxs_params_release(nxs_params_t *params)
{
	if (params->doc) {
		yyjson_mut_doc_free(params->doc);
	}
	free(params);
}

//////////////////////////////////////////////////////////////////////////

/*
 * nxs_params_get_strset: get the set of strings; the caller is responsible
 * to call free(3) on the returned list.
 */
const char **
nxs_params_get_strset(nxs_params_t *params, const char *key, size_t *count)
{
	yyjson_mut_val *arr = yyjson_mut_obj_get(params->root, key);
	size_t i = 0, c, idx, max;
	yyjson_mut_val *val;
	const char **strings;

	if (!arr || (c = yyjson_mut_arr_size(arr)) == 0) {
		return NULL;
	}
	if ((strings = calloc(c, sizeof(const char *))) == NULL) {
		return NULL;
	}
	yyjson_mut_arr_foreach(arr, idx, max, val) {
		if (yyjson_mut_is_str(val)) {
			strings[i++] = yyjson_mut_get_str(val);
		}
	}
	*count = i;
	return strings;
}

const char *
nxs_params_get_str(nxs_params_t *params, const char *key)
{
	yyjson_mut_val *jval = yyjson_mut_obj_get(params->root, key);
	return yyjson_mut_is_str(jval) ? yyjson_mut_get_str(jval) : NULL;
}

int
nxs_params_get_uint(nxs_params_t *params, const char *key, uint64_t *val)
{
	yyjson_mut_val *jval = yyjson_mut_obj_get(params->root, key);
	if (!yyjson_mut_is_uint(jval)) {
		return -1;
	}
	*val = yyjson_mut_get_uint(jval);
	return 0;
}

//////////////////////////////////////////////////////////////////////////

int
nxs_params_serialize(nxs_t *nxs, const nxs_params_t *params, const char *path)
{
	yyjson_write_err err;

	if (!yyjson_mut_write_file(path, params->doc,
	    YYJSON_WRITE_PRETTY, NULL, &err)) {
		nxs_decl_errx(nxs, "params serialize failed: %s", err.msg);
		return -1;
	}
	return 0;
}

static nxs_params_t *
nxs_params_load(nxs_t *nxs, yyjson_doc *doc, yyjson_read_err *err)
{
	nxs_params_t *params;

	if (!doc) {
		nxs_decl_errx(nxs, "params parsing failed: %s at %u",
		    err->msg, err->pos);
		return NULL;
	}
	if ((params = calloc(1, sizeof(nxs_params_t))) == NULL) {
		return NULL;
	}
	params->doc = yyjson_doc_mut_copy(doc, NULL);
	params->root = yyjson_mut_doc_get_root(params->doc);
	yyjson_doc_free(doc);
	return params;
}

nxs_params_t *
nxs_params_unserialize(nxs_t *nxs, const char *path)
{
	yyjson_read_err err;
	yyjson_doc *doc = yyjson_read_file(path, 0, NULL, &err);
	return nxs_params_load(nxs, doc, &err);
}

__dso_public nxs_params_t *
nxs_params_fromjson(nxs_t *nxs, const char *json, size_t len)
{
	yyjson_read_err err;
	yyjson_doc *doc = yyjson_read_opts(
	    (char *)(uintptr_t)json, len, 0, NULL, &err);
	return nxs_params_load(nxs, doc, &err);
}
