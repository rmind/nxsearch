#!/bin/sh

set -eu

base_url="http://127.0.0.1:8000/"

# Wait for HTTP port 8000 to be ready.
curl -s -o /dev/null -m 2 \
    --retry 5 --retry-delay 1 --retry-max-time 5 --retry-all-errors \
    $base_url

index="__test-index-svc-1"
curl -s -XPOST $base_url/$index
code=$(curl -s -I -XPOST $base_url/~ | awk '/^HTTP/ { print $2 }')
if [ "$code" != "400" ]; then
	echo "ERROR: expected HTTP 400 but got: $code" >&2
	exit 1
fi

curl -s -d "cat dog cow" $base_url/$index/add/1
curl -s -d "dog cow" $base_url/$index/add/2
curl -s -d "cat cat cat" $base_url/$index/add/3

results="$(curl -s -d "cat" $base_url/$index/search)"
doc_ids="$(echo "$results" | jq '.results[].doc_id' | xargs)"

curl -s -XDELETE $base_url/$index

expected="3 1"
if [ "$doc_ids" != "$expected" ]; then
	echo "ERROR: expected document IDs [ $expected ] but got:" >&2
	echo "$results" | jq >&2
	exit 1
fi

echo "OK"
