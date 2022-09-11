FROM debian:11.4

#
# Install dependencies.
#
RUN apt-get update -y
RUN apt-get install -y curl vim less
RUN apt-get install -y build-essential libtool libtool-bin gdb
RUN apt-get install -y pkg-config cmake debhelper
RUN apt-get install -y libjemalloc-dev libicu-dev libstemmer-dev

#
# Build.
#
WORKDIR /build
COPY ./src src

WORKDIR /build/src
RUN make distclean && make -j $(getconf _NPROCESSORS_ONLN) tests
