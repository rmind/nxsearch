FROM debian:11.5-slim

RUN apt-get update -y && \
    apt-get install -y curl vim less && \
    apt-get install -y build-essential libtool libtool-bin gdb gcovr && \
    apt-get install -y pkg-config cmake debhelper unzip libxml2-utils && \
    apt-get install -y libjemalloc-dev libicu-dev libstemmer-dev && \
    apt-get install -y re2c lemon python3-dev

WORKDIR /build-lib
COPY ./src /build-lib

RUN make distclean && \
    LIBDIR=/usr/lib INCDIR=/usr/include USE_LUA=0 \
    make install

WORKDIR /build
