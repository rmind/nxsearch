# nxsearch

![TESTS](https://github.com/rmind/nxsearch/actions/workflows/tests.yaml/badge.svg)

**Work in progress**. Upstream at: https://github.com/rmind/nxsearch

**nxsearch** is a full-text search engine library which is also provided
with a web server integration.

The engine is written in C11 and is distributed under the 2-clause BSD license.

## Features

- Lightweight, simple and fast.
- [BM25](https://en.wikipedia.org/wiki/Okapi_BM25)
and [TF-IDF](https://en.wikipedia.org/wiki/Tf%E2%80%93idf) algorithms.
- Integrates with the [Snowball stemmer](https://snowballstem.org/).
- Supports fuzzy matching (using the BK-tree with Levenshtein distance).
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

## Documentation

- Swagger UI with the endpoint documentation is provided at `/docs` URL.

- The Lua filters API can be found [HERE](docs/lua-filters-api.md).

- The C API documentation can be found [HERE](docs/c-api.md).
