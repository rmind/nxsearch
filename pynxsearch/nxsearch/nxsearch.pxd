#
# Copyright (c) 2025 Mindaugas Rasiukevicius <rmind at noxt eu>
# All rights reserved.
#
# Use is subject to license terms, as specified in the LICENSE file.
#

import cython
from libc.stdint cimport uint64_t
from libcpp cimport bool


cdef extern from "<nxs.h>":
    ctypedef struct nxs_t
    nxs_t *nxs_open(const char *)
    void nxs_close(nxs_t *)

    ctypedef enum nxs_err_t:
        NXS_ERR_SUCCESS
        NXS_ERR_FATAL
        NXS_ERR_SYSTEM
        NXS_ERR_INVALID
        NXS_ERR_EXISTS
        NXS_ERR_MISSING
        NXS_ERR_LIMIT
    nxs_err_t nxs_get_error(const nxs_t *, const char **)

    ctypedef struct nxs_params_t
    nxs_params_t *nxs_index_get_params(nxs_index_t *)
    nxs_params_t *nxs_params_fromjson(nxs_t *, const char *, size_t)
    char *nxs_params_tojson(const nxs_params_t *, size_t *)
    void nxs_params_release(nxs_params_t *)

    ctypedef struct nxs_index_t
    nxs_index_t *nxs_index_create(nxs_t *, const char *, nxs_params_t *)
    nxs_index_t *nxs_index_open(nxs_t *, const char *)
    void nxs_index_close(nxs_index_t *);
    int nxs_index_destroy(nxs_t *, const char *)

    ctypedef uint64_t nxs_doc_id_t
    int nxs_index_add(nxs_index_t *, nxs_params_t *, nxs_doc_id_t, const char *, size_t)
    int nxs_index_remove(nxs_index_t *, nxs_doc_id_t)

    ctypedef struct nxs_resp_t
    nxs_resp_t *nxs_index_search(nxs_index_t *, nxs_params_t *, const char *, size_t)
    void nxs_resp_release(nxs_resp_t *)

    void nxs_resp_iter_reset(nxs_resp_t *)
    bool nxs_resp_iter_result(nxs_resp_t *, nxs_doc_id_t *, float *)
    unsigned nxs_resp_resultcount(const nxs_resp_t *)
