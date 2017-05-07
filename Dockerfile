FROM ubuntu:trusty

# Installing dependencies
RUN apt-get update
RUN apt-get install -y git autotools-dev cdbs debhelper dh-autoreconf dpkg-dev gettext libev-dev libpcre3-dev libudns-dev pkg-config fakeroot

# Installing UDNS dependency
RUN mkdir -p /usr/src/udns
WORKDIR /usr/src/udns
RUN curl -O http://archive.ubuntu.com/ubuntu/pool/universe/u/udns/udns_0.4-1.dsc
RUN curl -O http://archive.ubuntu.com/ubuntu/pool/universe/u/udns/udns_0.4.orig.tar.gz
RUN curl -O http://archive.ubuntu.com/ubuntu/pool/universe/u/udns/udns_0.4-1.debian.tar.gz
RUN tar xfz udns_0.4.orig.tar.gz
WORKDIR /usr/src/udns/udns-0.4
RUN tar xfz ../udns_0.4-1.debian.tar.gz
RUN dpkg-buildpackage
WORKDIR /usr/src/udns
RUN dpkg -i libudns-dev_0.4-1_amd64.deb libudns0_0.4-1_amd64.deb

# Installing sniproxy
WORKDIR /usr/src/sniproxy
COPY . /usr/src/sniproxy
RUN ./autogen.sh && dpkg-buildpackage
RUN dpkg -i /usr/src/sniproxy_$(cat setver.sh | grep 'VERSION=[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*' | sed s/VERSION=//)_amd64.deb

# Cleaning installation dirs (smaller image), requires docker-squash
WORKDIR /root
RUN rm -rf /usr/src/

EXPOSE 80
EXPOSE 443

CMD sniproxy -f
