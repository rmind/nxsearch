#!/bin/sh

set -eu

project_dir="${1:?project directory required}"

if [ $(uname -s) = "Linux" ]; then
	os_env="$AUDITWHEEL_PLAT"
else
	os_env="darwin"
fi

echo "Preparing build environment for $os_env"

install_lemon()
{
	local github_base_url="https://raw.githubusercontent.com/"
	local lemon_tag="version-3.46.1"

	#
	# No package for AlmaLinux and Alpine.  Just build it.
	#
	curl -fsSL -o /tmp/lemon.c \
	    $github_base_url/sqlite/sqlite/$lemon_tag/tool/lemon.c
	curl -fsSL -o /usr/local/bin/lempar.c \
	    $github_base_url/sqlite/sqlite/$lemon_tag/tool/lempar.c
	cc -O2 -o /usr/local/bin/lemon /tmp/lemon.c
}

case "${os_env}" in
manylinux*)
	#
	# AlmaLinux (e.g. quay.io/pypa/manylinux_2_34_x86_64 image)
	#
	dnf install -y \
	    libtool pkgconf-pkg-config cmake libxml2 \
	    libicu-devel libstemmer-devel re2c
	install_lemon
	;;
musllinux*)
	#
	# Alpine (e.g. quay.io/pypa/musllinux_1_2_x86_64 image)
	#
	apk add --no-cache \
	    build-base libtool pkgconf cmake libxml2-utils \
	    icu-dev icu-data-full libstemmer-dev re2c
	install_lemon
	;;
darwin)
	#
	# Darwin.
	#
	brew install libtool cmake snowball re2c lemon
	export PKG_CONFIG_PATH="${DEPS_PREFIX:?missing}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
	export MACOSX_DEPLOYMENT_TARGET=11.0
	export LD_SHARED_CACHE_ELIGIBLE=NO
	;;
*)
	echo "ERROR: unsupported image '$os_env'" >&2
	exit 1
	;;
esac

#
# Build the nxsearch library
#
cd "$project_dir/src"
make distclean
LIBDIR=/usr/lib INCDIR=/usr/include USE_LUA=0 \
    make -j $(getconf _NPROCESSORS_ONLN) install
