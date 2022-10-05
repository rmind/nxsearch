#!/bin/sh

set -eu

curl -XPOST http://127.0.0.1:8000/test-idx

curl -XPOST --data "cat dog cow" http://127.0.0.1:8000/test-idx/add/1
curl -XPOST --data "dog cow" http://127.0.0.1:8000/test-idx/add/2
curl -XPOST --data "cat cat cat" http://127.0.0.1:8000/test-idx/add/3

curl -s -XPOST --data "cat" http://127.0.0.1:8000/test-idx/search | jq
