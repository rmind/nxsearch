FROM debian:11.4

#
# Install dependencies.
#
RUN apt-get update -y && \
    apt-get install -y curl vim less && \
    apt-get install -y build-essential libtool libtool-bin gdb && \
    apt-get install -y pkg-config cmake debhelper unzip libxml2-utils && \
    apt-get install -y libjemalloc-dev libicu-dev libstemmer-dev && \
    apt-get install -y luajit libluajit-5.1-dev lua-cjson

WORKDIR /nxsearch
ENV NXS_BASEDIR=/nxsearch

WORKDIR /build
COPY tools/fetch_ext_data.sh ./
RUN ./fetch_ext_data.sh "$NXS_BASEDIR"

COPY ./src /build

# Run the tests.
RUN make distclean && make -j $(getconf _NPROCESSORS_ONLN) tests

# Build the Lua lib and run the test.
RUN make distclean && make -j $(getconf _NPROCESSORS_ONLN) lua-lib
RUN luajit ./tests/test.lua
