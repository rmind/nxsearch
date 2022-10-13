/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#include <yyjson.h>
#pragma GCC diagnostic pop

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "rhashmap.h"
#include "heap.h"
#include "utils.h"

typedef struct {
	nxs_doc_id_t		doc_id;
	float			score;
	void *			next;
} result_entry_t;

struct nxs_resp {
	rhashmap_t *		doc_map;
	heap_t *		heap;
	size_t			count;

	result_entry_t *	results;

	char *			errmsg;
	int			errno;

	yyjson_mut_doc *	doc;
	yyjson_mut_val *	root;

	yyjson_mut_val *	results_arr;
	yyjson_mut_arr_iter	results_iter;
};

static int	result_entry_cmp(const void *, const void *);

nxs_resp_t *
nxs_resp_create(size_t limit)
{
	nxs_resp_t *resp;
	yyjson_mut_val *results_key;

	if ((resp = calloc(1, sizeof(nxs_resp_t))) == NULL) {
		return NULL;
	}

	/*
	 * Create the document map and the heap.
	 */
	resp->doc_map = rhashmap_create(0, RHM_NOCOPY | RHM_NONCRYPTO);
	if (resp->doc_map == NULL) {
		nxs_resp_release(resp);
		return NULL;
	}
	resp->heap = heap_create(limit, result_entry_cmp);
	if (resp->heap == NULL) {
		nxs_resp_release(resp);
		return NULL;
	}

	/*
	 * Create a new JSON document.
	 */
	resp->doc = yyjson_mut_doc_new(NULL);
	resp->root = yyjson_mut_obj(resp->doc);
	yyjson_mut_doc_set_root(resp->doc, resp->root);

	/*
	 * Prepare the results array.
	 */
	results_key = yyjson_mut_str(resp->doc, "results");
	resp->results_arr = yyjson_mut_arr(resp->doc);
	yyjson_mut_obj_add(resp->root, results_key, resp->results_arr);

	return resp;
}

/*
 * nxs_resp_release: destroy the response object.
 */
__dso_public void
nxs_resp_release(nxs_resp_t *resp)
{
	result_entry_t *entry = resp->results;

	if (resp->doc) {
		yyjson_mut_doc_free(resp->doc);
	}
	while (entry) {
		result_entry_t *next = entry->next;
		free(entry);
		entry = next;
	}
	if (resp->doc_map) {
		rhashmap_destroy(resp->doc_map);
	}
	if (resp->heap) {
		heap_destroy(resp->heap);
	}
	free(resp->errmsg);
	free(resp);
}

/*
 * nxs_resp_tojson: return the response as a JSON string.
 *
 * => The string must be released with free(3) by the caller.
 */
__dso_public char *
nxs_resp_tojson(nxs_resp_t *resp, size_t *len)
{
	return yyjson_mut_write(resp->doc, 0, len);
}

/*
 * nxs_resp_addresult: insert the new result entry (document ID and score)
 * or update the existing entry by adding the score.
 */
int
nxs_resp_addresult(nxs_resp_t *resp, const idxdoc_t *doc, float score)
{
	result_entry_t*entry;

	entry = rhashmap_get(resp->doc_map, &doc->id, sizeof(nxs_doc_id_t));
	if (entry) {
		entry->score += score;
		return 0;
	}

	if ((entry = calloc(1, sizeof(result_entry_t))) == NULL) {
		return -1;
	}
	rhashmap_put(resp->doc_map, &doc->id, sizeof(nxs_doc_id_t), entry);
	entry->doc_id = doc->id;
	entry->score = score;
	entry->next = resp->results;

	resp->results = entry;
	resp->count++;
	return 0;
}

static inline void
add_json_result_entry(nxs_resp_t *resp, result_entry_t *entry)
{
	yyjson_mut_val *resobj = yyjson_mut_obj(resp->doc);

	yyjson_mut_obj_add_uint(resp->doc, resobj, "doc_id", entry->doc_id);
	yyjson_mut_obj_add_real(resp->doc, resobj, "score", entry->score);
	yyjson_mut_arr_append(resp->results_arr, resobj);
}

/*
 * result_entry_cmp: comparator for the result entries.
 */
static int
result_entry_cmp(const void *p1, const void *p2)
{
	const result_entry_t *e1 = p1;
	const result_entry_t *e2 = p2;

	if (e1->score < e2->score)
		return -1;
	if (e1->score > e2->score)
		return 1;
	return 0;
}

/*
 * nxs_resp_build: finish up the response object (build any structures,
 * initialize the iterators, etc).
 */
void
nxs_resp_build(nxs_resp_t *resp)
{
	result_entry_t **top_results, *entry;

	/*
	 * Sort the entries with a cap and build the JSON entries.
	 */
	entry = resp->results;
	while (entry) {
		result_entry_t *next = entry->next;
		heap_add(resp->heap, entry);
		entry = next;
	}
	top_results = heap_sort(resp->heap, &resp->count);
	for (size_t i = 0; i < resp->count; i++) {
		add_json_result_entry(resp, top_results[i]);
	}
	heap_destroy(resp->heap);
	resp->heap = NULL;

	/*
	 * Destroy the entries.
	 */
	entry = resp->results;
	while (entry) {
		result_entry_t *next = entry->next;
		free(entry);
		entry = next;
	}

	rhashmap_destroy(resp->doc_map);
	resp->doc_map = NULL;
	resp->results = NULL;

	/* Set the count and initialize the iterator. */
	yyjson_mut_obj_add_uint(resp->doc, resp->root, "count", resp->count);
	yyjson_mut_arr_iter_init(resp->results_arr, &resp->results_iter);
}

__dso_public void
nxs_resp_iter_reset(nxs_resp_t *resp)
{
	yyjson_mut_arr_iter_init(resp->results_arr, &resp->results_iter);
}

__dso_public bool
nxs_resp_iter_result(nxs_resp_t *resp, nxs_doc_id_t *doc_id, float *score)
{
	yyjson_mut_val *result;

	result = yyjson_mut_arr_iter_next(&resp->results_iter);
	if (!result) {
		return false;
	}
	*doc_id = yyjson_mut_get_uint(yyjson_mut_obj_get(result, "doc_id"));
	*score = yyjson_mut_get_real(yyjson_mut_obj_get(result, "score"));
	return true;
}

__dso_public unsigned
nxs_resp_resultcount(const nxs_resp_t *resp)
{
	return resp->count;
}
