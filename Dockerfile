#
# build by: docker build -t sniproxy .
#

FROM ubuntu:20.04 AS builder

ENV DEBIAN_FRONTEND noninteractive
ENV TZ Asia/Tehran

# Build Stage
RUN apt-get update && \
    apt-get install -y \
        autotools-dev \
        cdbs \
        debhelper \
        dh-autoreconf \
        dpkg-dev \
        gettext \
        libev-dev \
        libpcre3-dev \
        libudns-dev \
        pkg-config \
        fakeroot \
        devscripts && \
    mkdir -p /sniproxy

COPY . /sniproxy/

RUN cd /sniproxy && \
    ./autogen.sh && \
    dpkg-buildpackage



# Usage
FROM ubuntu:20.04

COPY --from=builder /*.deb /tmp/

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        libev-dev \
        libudns-dev \
        libpcre3-dev && \
    dpkg -i /tmp/sniproxy_*.deb && \
    rm -f /tmp/sniproxy_*.deb

ENTRYPOINT ["sniproxy", "-f"]
EXPOSE 80 443
