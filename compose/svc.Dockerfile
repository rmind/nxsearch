#
# nxsearch library image
#

FROM debian:11.5-slim AS nxsearch

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

##############################################################################
FROM node:14-alpine as doc-gen
#
# nxsearch-svc swagger doc generation
#

WORKDIR /app

COPY ./svc-src/gen_doc_api.sh ./
COPY ./svc-src/nxsearch_svc.lua ./

RUN npm install swagger-inline --save-dev &&\
    sh gen_doc_api.sh > openapi.json

##############################################################################
#
# nxsearch-svc image
#

# OpenResty on Debian (11.x -- Bullseye)
FROM openresty/openresty:bullseye AS nxsearch-svc

RUN apt-get update
RUN apt-get install -y vim less procps net-tools
RUN apt-get install -y curl git unzip libxml2-utils
RUN apt-get install -y libicu67 libstemmer0d

#
# Install dependencies.
#
RUN apt-get install -y luarocks
RUN luarocks install resty-route 0.1-2
RUN luarocks install luafilesystem 1.8.0-1
RUN luarocks install lua-path 0.3.1-2

#
# Install Nginx/Openresty configuration.
#
COPY compose/nginx.conf /etc/nginx/nginx.conf
RUN ln -sf /etc/nginx/nginx.conf /usr/local/openresty/nginx/conf/nginx.conf

#
# Setup unprivileged user.
#
RUN useradd -m svc
RUN chown -R svc:svc /var/run/openresty/ /usr/local/openresty/nginx/
RUN chown -R root:root /usr/local/openresty/nginx/sbin/

#
# Data directory.
#
WORKDIR /nxsearch
RUN chown svc:svc /nxsearch
ENV NXS_BASEDIR=/nxsearch

WORKDIR /build
COPY tools/fetch_ext_data.sh ./
RUN ./fetch_ext_data.sh "$NXS_BASEDIR"

#
# Application
#

WORKDIR /app
COPY --from=nxsearch /build/nxsearch.so /usr/local/openresty/lualib/
COPY ./svc-src/nxsearch_svc.lua /usr/local/openresty/lualib/
COPY ./svc-src/nxsearch_storage.lua /usr/local/openresty/lualib/
COPY compose/docs.html /app/public_html/docs.html
COPY --from=doc-gen /app/openapi.json /app/public_html/openapi.json


#
# Run the service.
#
USER svc
EXPOSE 8000
CMD ["/usr/bin/openresty"]
