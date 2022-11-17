# nxsearch C API

## General

The public API is provided by the `<nxs.h>` header.  The library is not
mult-thread safe but the underlying structures support concurrency which
can be utilized using separate processes.  A single library instance is
represented by the `nxs_t *` reference.

* `nxs_t *nxs_open(const char *basedir)`
  * Open a library instance using the given base directory where the
  index and filter data would be stored and managed. The base directory
  must already exist. If the `basedir` parameter is `NULL`, then the
  `NXS_BASEDIR` environment variable will be used instead.

* `void nxs_close(nxs_t *nxs)`
  * Close the library instance. This will also close any opened indexes,
  making the existing references no longer valid.

* `nxs_err_t nxs_get_error(const nxs_t *nxs, const char **error_msg)`
  * Get the last recorded error. This is used to retrieve the error code
  with the associated message for the last failed operation.  See the section
  on [errors](#errors) below for more details.

### Errors

The error code is represented by the `nxs_err_t` enumeration.  The list
of symbolic error names defined:

* `NXS_ERR_SUCCESS`: indicate the success of last operation i.e. no error.
The value is this error code can be assumed to be zero.
* `NXS_ERR_FATAL`: unspecified fatal error; generally not expected to occur,
unless index files get corrupted, there is an unusual system-level error
condition or a flaw in the application.
* `NXS_ERR_SYSTEM`: system-level error, e.g. access denial or out-of-memory.
* `NXS_ERR_INVALID`: invalid parameter or value, e.g. invalid ranking algorithm.
* `NXS_ERR_EXISTS`: resource already exists, e.g. document ID already exists.
* `NXS_ERR_MISSING`: resource is missing, e.g. index was not created.
* `NXS_ERR_LIMIT`: resource limit reached, e.g. term is too large.

## Parameters

The `nxs_params_t` object is used to pass the index or query parameters.
It is a container for key-value pairs representing some configuration.

* `nxs_params_t *nxs_params_create(void)`
  * Create a parameters object, which is initially just an empty container.

* `nxs_params_t *nxs_params_fromjson(nxs_t *nxs, const char *text, size_t len)`
  * Create a parameters object populated from from the JSON string, specified
  by `text` and its length by `len`.

* `void nxs_params_release(nxs_params_t *params)`
  * Destroy the parameters object.

* `int nxs_params_set_str(nxs_params_t *params, const char *key, const char *val)`
  * Set the key to the given string value.  Returns `0` on success or non-zero
  on error.

* `int nxs_params_set_uint(nxs_params_t *params, const char *key, uint64_t *val)`
  * Set the key to the given integer value.  Returns `0` on success or non-zero
  on error.

* `char *nxs_params_tojson(const nxs_params_t *params, size_t *len)`
  * Return parameters as a JSON string (or `NULL` on failure).  The length
  is stored in `len` parameter if it is non-NULL.  The user is responsible
  for calling `free(3)` on the string after use.

## Index

The `nxs_index_t *` is an active reference to an index.

* `nxs_index_t *nxs_index_create(nxs_t *, const char *name, nxs_params_t *params)`
  * Create new index with a given `name` and parameters. If `params` is `NULL`,
  then the default parameters will be used.  Returns the index reference or
  `NULL` on failure.  Currently, the following parameters are supported:
    * `algo`: ranking algorithm; it may be set to "BM25" (default) or "TF-IDF".
    * `lang`: language of the content in the index which should be two-letter
    ISO 639-1 code; "en" (English) is the default.
    * `filters`: a list of filters (in the specified order) to apply when
    tokenizing; default is: "normalizer", "stopwords", "stemmer".

* `nxs_index_t *nxs_index_open(nxs_t *nxs, const char *name)`
  * Open the index specified by `name` loading the internal tracking structures
  into the memory.  Only one reference may be open per library instance.
  Returns the index reference or `NULL` on failure.

* `void nxs_index_close(nxs_index_t *idx)`
  * Close the index, releasing its in-memory structures.

* `int nxs_index_destroy(nxs_t *nxs, const char *name)`
  * Destroy the index, specified by `name`, deleting all of its data.

* `nxs_params_t *nxs_index_get_params(nxs_index_t *idx)`
  Get the current parameters of the index. This is an active reference which
  must not be destroyed with `nxs_params_release()`.

## Add/remove documents

* `int nxs_index_add(nxs_index_t *idx, nxs_params_t *params,
  nxs_doc_id_t id, const char *text, size_t len)`
  * Index the given document.  The caller must provide a unique document ID,
  specified by `id` which must be a non-zero 64-bit integer.  The document
  content is provided by `text` (with its length by `len` in bytes) which may
  be in UTF-8.  Returns 0 on success or non-zero on failure.  Currently, no
  parameters are supported and the `params` value should be NULL, but this
  may change in the future.

* `int nxs_index_remove(nxs_index_t *idx, nxs_doc_id_t id)`
  * Remove the document from the index.  Returns 0 on success or non-zero
  on failure.

## Query and results

The `nxs_resp_t *` is a reference to a response containing the results.
It is the user's responsibility to release them after use.

* `nxs_resp_t *nxs_index_search(nxs_index_t *idx, nxs_params_t *params,
  const char *query, size_t len)`
  * Search the index using the `query` string (with its length in `len`).
  Custom search parameters may be specified in `params` or it may be set
  to `NULL` for defaults.  Returns `NULL` on failure or the response object
  on success.  The response object must be released using `nxs_resp_release()`.
  Currently, the following parameters are supported:
    * `algo`: override ranking algorithm (see `nxs_index_create()` description).
    * `limit`: the cap for the results (default: 1000).
    * `fuzzymatch`: fuzzy-match the terms (default: true).

* `char *nxs_resp_tojson(nxs_resp_t *resp, size_t *len)`
  * Return the response as a JSON string representation.  If the `len` is not
  NULL, stores the length of string in it. The user is responsible for calling
  `free(3)` on the string after use.  Returns `NULL` on failure.

* `void nxs_resp_release(nxs_resp_t *resp)`
  * Release the associated structure and destroy the response object.

The results can also be iterated using the following API:

* `void nxs_resp_iter_reset(nxs_resp_t *resp)`
  * Initialize or reset the iterator to the beginning.

* `bool nxs_resp_iter_result(nxs_resp_t *resp, nxs_doc_id_t *id, float *score)`
  * Returns `true` and the current document ID with its associated score
  and advanced the iterator to next item.  Returns `false` when the end is
  reached (or if there are no results).

* `unsigned nxs_resp_resultcount(const nxs_resp_t *resp)`
  * Return the number of items in the results.
