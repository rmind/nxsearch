#!/bin/sh

set -eu

curl -s -XPOST http://127.0.0.1:8000/test-idx

curl -s -XPOST --data "cat dog cow" http://127.0.0.1:8000/test-idx/add/1
curl -s -XPOST --data "dog cow" http://127.0.0.1:8000/test-idx/add/2
curl -s -XPOST --data "cat cat cat" http://127.0.0.1:8000/test-idx/add/3

result="$(curl -s -XPOST --data "cat" http://127.0.0.1:8000/test-idx/search | \
    jq '.results[].doc_id' | sed -z 's/\n/,/g;s/,$/\n/' )"

curl -s -XDELETE http://127.0.0.1:8000/test-idx

expected="3,1"

if [ "$result" != "$expected" ]; then
    echo "Found doc ids: [${result[@]}] expected: [${expected[@]}]"
    exit 1
fi

echo "OK"
