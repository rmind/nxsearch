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

    index.add(1, "cat dog cow")
    index.add(2, "dog cow")
    index.add(3, "cat cat cat")
    index.add(4, "cat's catnip")

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


def test_nxsearch_missing(tmp_path):
    nxs = nxsearch.init(tmp_path)
    with pytest.raises(NxsResourceMissingError):
        nxs.open("test")


def test_nxsearch_destroy(tmp_path):
    nxs = nxsearch.init(tmp_path)
    nxs.open("test", create=True)
    nxs.destroy("test")
