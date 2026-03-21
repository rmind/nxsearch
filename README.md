# nxsearch

![TESTS](https://github.com/rmind/nxsearch/actions/workflows/tests.yaml/badge.svg)

**nxsearch** is a full-text search engine library which is also provided
with a web server integration.

The engine is written in C11 and is distributed under the 2-clause BSD license.

Upstream at: https://github.com/rmind/nxsearch

## Features

- Lightweight, simple and fast.
- [BM25](https://en.wikipedia.org/wiki/Okapi_BM25)
and [TF-IDF](https://en.wikipedia.org/wiki/Tf%E2%80%93idf) algorithms.
- Integrates with the [Snowball stemmer](https://snowballstem.org/).
- Supports fuzzy matching (using the BK-tree with Levenshtein distance).
- Supports query logic operators, grouping, nesting, etc.
- Supports filters in Lua for easy extendibility.
- Basic UTF-8 and internationalization support.

## Usage

To try as a web service:
```shell
# git submodule update --init --recursive  # ensure you have submodules
docker-compose up app  # spin up the service
open http://127.0.0.1:8000/docs  # documentation page
```

The `NXS_BASEDIR` environment variable specifies the base directory where
the indexed documents as well as the application data files are stored.

### Shell

```shell
# Create the index:
curl -XPOST http://127.0.0.1:8000/test-idx

# Index some test documents:
curl -d "cat dog cow" http://127.0.0.1:8000/test-idx/add/1
curl -d "dog cow" http://127.0.0.1:8000/test-idx/add/2
curl -d "cat cat cat" http://127.0.0.1:8000/test-idx/add/3

# Run a query:
curl -s -d "cat" http://127.0.0.1:8000/test-idx/search | jq
```

### Python

You can also use the nxsearch as a Python library.

```python
import nxsearch
from nxsearch import NxsResourceExistsError

nxs = nxsearch.init("./nxs-data")
index = nxs.open("animal-articles", create=True)

index.add(1, "cat dog cow")
index.add(2, "dog cow")
index.add(3, "cat cat cat")
index.add(4, "cat's catnip")

with index.search("cat") as result:
    for item in result:
        print(item)
```

## Documentation

- Swagger UI with the endpoint documentation is provided at `/docs` URL.

- The C API documentation can be found [HERE](docs/c-api.md).

- The Lua filters API can be found [HERE](docs/lua-filters-api.md).
