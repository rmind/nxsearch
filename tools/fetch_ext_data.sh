#!/bin/sh

basedir="$1"
set -eu

if [ ! -d "$basedir" ]; then
	echo "ERROR: given path is not a directory" >&2
	exit 1
fi

if ! type xmllint > /dev/null 2>&1; then
	echo "ERROR: missing xmllint; please install libxml2 package." >&2
	exit 1
fi

lang_map=$(cat <<EOF
english:en
spanish:es
german:de
french:fr
EOF
)

get_package_url()
{
	local github_content_base="https://raw.githubusercontent.com"
	local nltk_data_index="${github_content_base}/nltk/nltk_data/gh-pages/index.xml"
	local package="$1"

	curl -s "$nltk_data_index" | \
	    xmllint --xpath "string(//packages/package[@id='${package}']/@url)" -
}

get_package()
{
	local file="stopwords.zip"
	local url=$(get_package_url "stopwords")

	mkdir -p "$basedir/filters"
	cd "$basedir/filters"

	curl -s -o "$file" "$url"
	unzip -q "$file" && rm "$file"

	# Translate to two-letter ISO 3166-1 codes.
	cd stopwords
	for lmap in $lang_map; do
		local name=$(echo "$lmap" | cut -d: -f1)
		local code=$(echo "$lmap" | cut -d: -f2)
		mv "$name" "$code"
	done
}

printf "Fetching the stopwords .. "
get_package
echo "done."
