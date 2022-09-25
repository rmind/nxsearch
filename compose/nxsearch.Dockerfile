FROM debian:11.4

#
# Install dependencies.
#
RUN apt-get update -y && \
    apt-get install -y curl vim less && \
    apt-get install -y build-essential libtool libtool-bin gdb && \
    apt-get install -y pkg-config cmake debhelper && \
    apt-get install -y libjemalloc-dev libicu-dev libstemmer-dev && \
    apt-get install -y luajit libluajit-5.1-dev lua5.4 liblua5.4-dev

WORKDIR /build
COPY ./src /build

# Run the tests.
RUN make distclean && make -j $(getconf _NPROCESSORS_ONLN) tests

# Build the Lua lib.
RUN make distclean && make -j $(getconf _NPROCESSORS_ONLN) lua-lib
