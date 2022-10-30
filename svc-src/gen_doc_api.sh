#!/bin/sh
set -eu

awk '/\[\[/,/\]\]/' nxsearch_svc.lua \
    | sed 's/\-\-\[\[/\/\*/' \
    | sed 's/\-\-\]\]/\*\//' \
> lua.js

cat <<EOT >> base.yml
openapi: "3.0.2"
info:
    title: "Swagger NXSearch"
    description: ""

EOT
npx swagger-inline lua.js --base base.yml -f .json
