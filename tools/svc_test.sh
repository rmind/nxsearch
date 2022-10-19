#!/bin/sh

set -eu

curl -s -XPOST http://127.0.0.1:8000/test-idx

curl -s -XPOST --data "cat dog cow" http://127.0.0.1:8000/test-idx/add/1
curl -s -XPOST --data "dog cow" http://127.0.0.1:8000/test-idx/add/2
curl -s -XPOST --data "cat cat cat" http://127.0.0.1:8000/test-idx/add/3

results="$(curl -s -XPOST --data "cat" http://127.0.0.1:8000/test-idx/search)"
doc_ids="$(echo "$results" | jq '.results[].doc_id' | xargs)"

curl -s -XDELETE http://127.0.0.1:8000/test-idx

expected="3 1"
if [ "$doc_ids" != "$expected" ]; then
	echo "ERROR: expected document IDs [ $expected ] but got:" >&2
	echo "$results" | jq >&2
	exit 1
fi

echo "OK"
