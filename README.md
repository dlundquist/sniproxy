SNI Proxy
=========

Proxies incoming HTTP and TLS connections based on the hostname contained in
the initial request. This enables HTTPS name-based virtual hosting to separate
backend servers without installing the private key on the proxy machine.

Features
--------
+ Name-based proxying of HTTPS without decrypting traffic. No keys or
  certificates required.
+ Supports both TLS and HTTP protocols.
+ Supports IPv4, IPv6 and Unix domain sockets for both back end servers and
  listeners.
+ Supports multiple listening sockets per instance.

Usage
-----

    Usage: sniproxy [-c <config>] [-f]
        -c  configuration file, defaults to /etc/sniproxy.conf
        -f  run in foreground, do not drop privileges


Installation
------------

For Debian or Fedora based Linux distributions see building packages below.

**Prerequisites**

+ Autotools (autoconf, automake and libtool)
+ libev4, libpcre and libudns development headers
+ Perl and cURL for test suite

**Install**

    ./autogen.sh && ./configure && make check && sudo make install

**Building Debian/Ubuntu package**

This is the preferred installation method on recent Debian based distributions:

1. Install required packages

        sudo apt-get install dpkg-dev cdbs debhelper dh-autoreconf libev-dev libpcre3-dev libudns-dev pkg-config

2. Build a Debian package

        dpkg-buildpackage

3. Install the resulting package

        sudo dpkg -i ../sniproxy_<version>_<arch>.deb

***Note on Upgrading***

The version of sniproxy is not automatically updated after each commit, so if
you are upgrading to a later version, the version number of the sniproxy package
may not have actually changed. This may cause issues with the upgrade process.
It is recommended you uninstall `sudo apt-get remove sniproxy` then reinstall
the new version.

**Building Fedora/RedHat package**

This is the preferred installation method for modern Fedora based distributions.

1. Install required packages

        sudo yum install rpmbuild autoconf automake curl libev-devel pcre-devel perl pkgconfig udns-devel

2. Build a distribution tarball:

        ./autogen && ./configure && make dist

3. Build a RPM package

        rpmbuild --define "_sourcedir `pwd`" -ba redhat/sniproxy.spec

4. Install resulting RPM

        sudo yum install ../sniproxy-<version>.<arch>.rpm

I've used Scientific Linux 6 a fair amount, but I prefer Debian based
distributions. I do not test building RPMs frequently (SL6 doesn't have a
libev-devel package). This build process may not follow the current Fedora
packaging standards, and may not even work.


Configuration Syntax
--------------------

    user daemon

    pidfile /tmp/sniproxy.pid

    error_log {
        syslog daemon
        priority notice
    }

    listener 127.0.0.1:443 {
        protocol tls
        table TableName

        # Specify a server to use if the initial client request doesn't contain
        # a hostname
        fallback 192.0.2.5:443
    }

    table TableName {
        # Match exact request hostnames
        example.com 192.0.2.10:4343
        example.net [2001:DB8::1:10]:443
        # Or use regular expression to match
        .*\\.com    [2001:DB8::1:11]:443
        # Combining regular expression and wildcard will resolve the hostname
        # client requested and proxy to it
        .*\\.edu    *:443
    }

DNS Resolution
--------------

Using hostnames or wildcard entries in the configuration requires sniproxy to
be built with [UDNS](http://www.corpit.ru/mjt/udns.html). SNIProxy will still
build without UDNS, but these features will be unavailable.

UDNS uses a single UDP socket for all queries, so it is recommended you use a
local caching DNS resolver (with a single socket each DNS query is protected by
spoofing by a single 16 bit query ID, which makes it relatively easy to spoof).

UDNS is currently not available in Debian stable, but a package can be easily built from the Debian testing or Ubuntu source packages:

    mkdir udns_packaging
    cd udns_packaging
    wget http://archive.ubuntu.com/ubuntu/pool/universe/u/udns/udns_0.4-1.dsc
    wget http://archive.ubuntu.com/ubuntu/pool/universe/u/udns/udns_0.4.orig.tar.gz
    wget http://archive.ubuntu.com/ubuntu/pool/universe/u/udns/udns_0.4-1.debian.tar.gz
    tar xfz udns_0.4.orig.tar.gz
    cd udns-0.4/
    tar xfz ../udns_0.4-1.debian.tar.gz
    dpkg-buildpackage
    cd ..
    sudo dpkg -i libudns-dev_0.4-1_amd64.deb libudns0_0.4-1_amd64.deb

