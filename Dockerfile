FROM ubuntu as builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
	apt-get install -y autotools-dev cdbs debhelper dh-autoreconf dpkg-dev gettext libev-dev libpcre3-dev libudns-dev pkg-config fakeroot devscripts

WORKDIR /usr/src/app
COPY . /usr/src/app

RUN ./autogen.sh && dpkg-buildpackage

FROM ubuntu
COPY --from=builder /usr/src/sniproxy_*.deb /root

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
	apt-get install -y libev4 libudns0 && \
	dpkg -i /root/sniproxy_*.deb && \
	rm -rf /var/lib/apt/lists/*

CMD sniproxy -f -c /etc/sniproxy.conf