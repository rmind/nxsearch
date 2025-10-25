#
# Copyright (c) 2025 Mindaugas Rasiukevicius <rmind at noxt eu>
# All rights reserved.
#
# Use is subject to license terms, as specified in the LICENSE file.
#

import cython
from typing import Optional, Dict
from libc.errno cimport errno
from libc.stdlib cimport free

from dataclasses import dataclass
from os import strerror
import json

from nxsearch.exceptions import (
    NxsFatalError,
    NxsSystemError,
    NxsInvalidValueError,
    NxsResourceExistsError,
    NxsResourceMissingError,
    NxsResourceLimitError,
)

@dataclass
class NxsResultItem:
    document_id: int
    score: float


@cython.final
cdef class NxsIndexParams:
    """
    A class for wrap the parameters.
    """
    cdef nxs_params_t *_c_nxs_params

    def __cinit__(self, nxs_ref, params: Optional[Dict]):
        if not params:
            # Simplifies the code.
            self._c_nxs_params = NULL
            return
        if not isinstance(params, dict):
            raise TypeError("parameter `params` is not a dict type")
        c_nxs = (<Nxs?>nxs_ref)._c_nxs
        params_json_b = json.dumps(params).encode()
        self._c_nxs_params = nxs_params_fromjson(
            c_nxs, params_json_b, len(params_json_b)
        )

    def __dealloc__(self):
        if self._c_nxs_params:
            nxs_params_release(self._c_nxs_params)
            self._c_nxs_params = NULL

    cdef nxs_params_t *get_params_ref(self):
        return self._c_nxs_params if self._c_nxs_params else NULL


@cython.final
cdef class NxsResult:
    """
    Search result object. It is used as an iterator that returns
    NxsResultItem() objects. Should generally be used with a context manager.
    """

    cdef nxs_resp_t *_c_nxs_resp
    cdef object nxs_index_ref

    @staticmethod
    cdef get_instance(NxsIndex idx, nxs_resp_t *resp):
        cdef NxsResult result = NxsResult()
        result.nxs_index_ref = <NxsIndex?>idx  # acquire a reference
        result._c_nxs_resp = resp
        return result

    def __dealloc__(self):
        if self._c_nxs_resp:
            nxs_resp_release(self._c_nxs_resp)
        self.nxs_index_ref = None  # release the reference

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        if self._c_nxs_resp:
            nxs_resp_release(self._c_nxs_resp)
            self._c_nxs_resp = NULL
        self.nxs_index_ref = None

    def __iter__(self):
        if self._c_nxs_resp:
            nxs_resp_iter_reset(self._c_nxs_resp)
        return self

    def __next__(self):
        cdef nxs_doc_id_t doc_id
        cdef float score

        if not self._c_nxs_resp:
            raise RuntimeError("the NxsResult object has no data")

        if not nxs_resp_iter_result(self._c_nxs_resp, &doc_id, &score):
            raise StopIteration

        return NxsResultItem(doc_id, score)

    def __len__(self):
        return (
            nxs_resp_resultcount(self._c_nxs_resp)
            if self._c_nxs_resp else 0
        )


@cython.final
cdef class NxsIndex:
    """
    A class that represents nxsearch index.
    """

    cdef nxs_index_t *_c_nxs_index
    cdef object nxs_ref

    def __cinit__(
        self,
        nxs_ref,
        name: str,
        params: Optional[Dict] = None,
        create: bool = False
    ):
        self.nxs_ref = nxs_ref  # acquire the reference first
        c_nxs = (<Nxs?>nxs_ref)._c_nxs
        bname = str(name).encode()

        self._c_nxs_index = nxs_index_open(c_nxs, bname)
        if self._c_nxs_index is NULL and create:
            c_params = NxsIndexParams(nxs_ref, params)
            self._c_nxs_index = nxs_index_create(
                c_nxs, bname, c_params.get_params_ref()
            )

        if self._c_nxs_index is NULL:
            self._throw_error()
            return

    def __dealloc__(self):
        if self._c_nxs_index:
            nxs_index_close(self._c_nxs_index)
        self.nxs_ref = None  # release the reference *after* index close

    def _throw_error(self):
        return self.nxs_ref._throw_error()

    def get_params(self) -> Dict:
        """
        Get index parameters (configuration) as a dict.
        """
        cdef nxs_params_t *c_params
        cdef bytes params_json

        # nxs_index_get_params() returns an active reference; do not destroy.
        c_params = nxs_index_get_params(self._c_nxs_index)
        if c_params is NULL:
            return None

        c_params_json = nxs_params_tojson(c_params, NULL)
        try:
            params_json = c_params_json
        finally:
            free(c_params_json)

        return json.loads(params_json.decode("utf-8"))

    def add(
        self,
        doc_id: int,
        content: str,
        params: Optional[Dict] = None
    ) -> int:
        """
        Index the given document. The caller must provide a unique document ID,
        specified by `doc_id` which must be a non-zero 64-bit integer.

        Returns the document ID on success.
        """

        if not isinstance(doc_id, int):
            raise TypeError("parameter `doc_id` is not an integer type")
        if doc_id <= 0:
            raise TypeError("parameter `doc_id` must be a positive integer")
        if not isinstance(content, str):
            raise TypeError("parameter `content` is not a string type")

        cdef nxs_doc_id_t c_doc_id = doc_id
        bcontent = str(content).encode()
        if nxs_index_add(
            self._c_nxs_index, NULL, c_doc_id, bcontent, len(bcontent)
        ) != 0:
            return self._throw_error()
        return doc_id

    def remove(self, doc_id: int):
        """
        Remove the document from the index.
        """

        if not isinstance(doc_id, int):
            raise TypeError("parameter `doc_id` is not an integer type")
        if doc_id <= 0:
            raise TypeError("parameter `doc_id` must be a positive integer")
        cdef nxs_doc_id_t c_doc_id = doc_id
        if nxs_index_remove(self._c_nxs_index, c_doc_id) != 0:
            return self._throw_error()

    def search(self, query: str, params: Optional[Dict] = None) -> NxsResult:
        """
        Search the index using the query string.
        """

        if not isinstance(query, str):
            raise TypeError("parameter `query` is not a string type")
        bquery = str(query).encode()
        resp = nxs_index_search(self._c_nxs_index, NULL, bquery, len(bquery))
        if resp is NULL:
            return self._throw_error()
        return NxsResult.get_instance(self, resp)


@cython.final
cdef class Nxs:
    """
    A general nxsearch library instance.
    """

    ERROR_MAP = {
        NXS_ERR_FATAL: NxsFatalError,
        NXS_ERR_SYSTEM: NxsSystemError,
        NXS_ERR_INVALID: NxsInvalidValueError,
        NXS_ERR_EXISTS: NxsResourceExistsError,
        NXS_ERR_MISSING: NxsResourceMissingError,
        NXS_ERR_LIMIT: NxsResourceLimitError,
    }

    cdef nxs_t *_c_nxs

    def __cinit__(self, basedir: str):
        _c_basedir = str(basedir).encode()
        self._c_nxs = nxs_open(_c_basedir)
        if self._c_nxs is NULL:
            raise OSError(errno, strerror(errno))

    def __dealloc__(self):
        if self._c_nxs is not NULL:
            nxs_close(self._c_nxs)

    def _throw_error(self):
        cdef const char *c_errmsg
        cdef bytes py_errmsg

        errcode = nxs_get_error(self._c_nxs, &c_errmsg)
        if errcode:
            exc = self.ERROR_MAP.get(errcode) or RuntimeError
            py_errmsg = c_errmsg
            raise exc(py_errmsg.decode("utf-8"))

    def open(
        self,
        name: str,
        params: Optional[Dict] = None,
        create: bool = False
    ):
        """
        Create a new index with a given `name` and parameters or, if already
        exists, open the existing index.
        """
        return NxsIndex(self, name, params=params, create=create)

    def destroy(self, name):
        """
        Destroy the index, specified by `name`, deleting all of its data.
        """

        c_name = str(name).encode()
        if nxs_index_destroy(self._c_nxs, c_name) != 0:
            return self._throw_error()

    def create_params(self, params: Dict) -> NxsIndexParams:
        """
        Create the parameters for repeated usage.
        """
        return NxsIndexParams(self, params)


def init(basedir):
    return Nxs(str(basedir))
