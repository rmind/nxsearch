# OpenResty on Debian (11.x -- Bullseye)
FROM openresty/openresty:bullseye

RUN apt-get update
RUN apt-get install -y vim less procps net-tools
RUN apt-get install -y curl git unzip libxml2-utils
RUN apt-get install -y libicu67 libstemmer0d

#
# Install dependencies.
#
RUN apt-get install -y luarocks
RUN luarocks install resty-route

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
# Data
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

#
# Run the service.
#
USER svc
EXPOSE 8000
CMD ["/usr/bin/openresty"]
