FROM debian:13.1-slim

RUN apt-get update -y && \
    apt-get install -y curl vim less && \
    apt-get install -y build-essential libtool libtool-bin gdb gcovr && \
    apt-get install -y pkg-config cmake debhelper unzip libxml2-utils && \
    apt-get install -y libicu-dev libstemmer-dev re2c lemon && \
    apt-get install -y python3-dev python-is-python3 cython3 python3-pip
RUN pip install --break-system-packages uv

WORKDIR /build-lib
COPY ./src /build-lib

RUN make distclean && USE_LUA=0 make -j $(getconf _NPROCESSORS_ONLN) install
RUN ldconfig || true

WORKDIR /build
COPY ./pynxsearch /build/pynxsearch

WORKDIR /build/pynxsearch
RUN make clean && make
RUN uv run pytest -vvv tests
