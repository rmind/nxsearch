import pytest

import nxsearch
from nxsearch import NxsResultItem
from nxsearch.exceptions import (
    NxsResourceExistsError,
    NxsResourceMissingError,
    NxsSystemError,
)


def test_nxsearch_basic(tmp_path):
    nxs = nxsearch.init(tmp_path)
    try:
        nxs.destroy("animal-articles")
    except:
        pass
    index = nxs.open("animal-articles", create=True)

    index.add("cat dog cow", doc_id=1)
    index.add("dog cow", doc_id=2)
    index.add("cat cat cat", doc_id=3)
    index.add("cat's catnip", doc_id=4)

    with index.search("cat") as result:
        results = [item for item in result]
    assert results == [
        NxsResultItem(
            document_id=3,
            score=pytest.approx(0.16284865140914917),
        ),
        NxsResultItem(
            document_id=4,
            score=pytest.approx(0.13059112429618835),
        ),
        NxsResultItem(
            document_id=1,
            score=pytest.approx(0.10551118105649948),
        ),
    ]


def test_nxsearch_auto_doc_id(tmp_path):
    nxs = nxsearch.init(tmp_path)
    index = nxs.open("animal-articles", create=True)
    index.add("cat")
    index.add("cats")
    with index.search("cat") as result:
        doc_ids = {item.document_id for item in result}
    assert doc_ids == {1, 2}


def test_nxsearch_missing(tmp_path):
    nxs = nxsearch.init(tmp_path)
    with pytest.raises(NxsResourceMissingError):
        nxs.open("test")


def test_nxsearch_destroy(tmp_path):
    nxs = nxsearch.init(tmp_path)
    nxs.open("test", create=True)
    nxs.destroy("test")
