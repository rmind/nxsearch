#!/bin/sh

set -eu

index="__test-index-svc-1"
curl -s -XPOST http://127.0.0.1:8000/$index
code=$(curl -s -I -XPOST http://127.0.0.1:8000/~ | awk '/^HTTP/ { print $2 }')
if [ "$code" != "400" ]; then
	echo "ERROR: expected HTTP 400 but got: $code" >&2
	exit 1
fi

curl -s -d "cat dog cow" http://127.0.0.1:8000/$index/add/1
curl -s -d "dog cow" http://127.0.0.1:8000/$index/add/2
curl -s -d "cat cat cat" http://127.0.0.1:8000/$index/add/3

results="$(curl -s -d "cat" http://127.0.0.1:8000/$index/search)"
doc_ids="$(echo "$results" | jq '.results[].doc_id' | xargs)"

curl -s -XDELETE http://127.0.0.1:8000/$index

expected="3 1"
if [ "$doc_ids" != "$expected" ]; then
	echo "ERROR: expected document IDs [ $expected ] but got:" >&2
	echo "$results" | jq >&2
	exit 1
fi

echo "OK"
