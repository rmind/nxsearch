FROM debian:11.4

#
# Install dependencies.
#
RUN apt-get update -y && \
    apt-get install -y curl vim less && \
    apt-get install -y build-essential libtool libtool-bin gdb && \
    apt-get install -y pkg-config cmake debhelper && \
    apt-get install -y libjemalloc-dev libicu-dev libstemmer-dev && \
    apt-get install -y lua5.4 liblua5.4-dev

#
# Build.
#
WORKDIR /build
COPY ./src /build
RUN make distclean && make -j $(getconf _NPROCESSORS_ONLN) tests
