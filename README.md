SNI Proxy
=========

Proxies incoming HTTP and TLS connections based on the hostname contained in
the initial request of the TCP session. This enables HTTPS name-based virtual
hosting to separate backend servers without installing the private key on the
proxy machine.

Status: Deprecated
------------------
2023-12-13

When I started this project, there wasn't another proxy that filled this niche.
Now, there are many proxies available to proxy layer-4 based on the TLS SNI
extension, including Nginx. Additionally, web traffic is evolving: with HTTP/2,
multiple hostnames can be multiplexed in a single TCP stream [preventing SNI
Proxy](https://github.com/dlundquist/sniproxy/issues/178) from routing it
correctly based on hostname, and HTTP/3 (QUIC) uses UDP transport. SNI Proxy
just doesn't support these protocols, and adding support for them would
complicate it significantly. For these reasons, I'm transitioning SNI Proxy to
a deprecated status.

Honestly, this has been the case for last several years, and I hadn't published
anything to that affect. With CVE-2023-25076 it became clear that this
situation needs to be communicated clearly.

In some cases, SNI Proxy might be a better fit than a more general purpose
proxy, so I'm not going to abandon the project completely. I'll still monitor
issues and email requests; however, unless it is a significant security or
reliablity issue, don't expect a response.

Going forward, I'm not sure about the value of libpcre3 to libpcre2 migration.
I see benefits of migrating to the modern version of the library, but I don't
want to break SNI proxy on older platforms. Also, libpcre3 is still widely
available. C++17 std::regex would be another option, but it adds a difficult
dependency for running on older platforms.

Features
--------
+ Name-based proxying of HTTPS without decrypting traffic. No keys or
  certificates required.
+ Supports both TLS and HTTP protocols.
+ Supports IPv4, IPv6 and Unix domain sockets for both back-end servers and
  listeners.
+ Supports multiple listening sockets per instance.
+ Supports HAProxy proxy protocol to propagate original source address to
  back-end servers.

Usage
-----

    Usage: sniproxy [-c <config>] [-f] [-n <max file descriptor limit>] [-V]
        -c  configuration file, defaults to /etc/sniproxy.conf
        -f  run in foreground, do not drop privileges
        -n  specify file descriptor limit
        -V  print the version of SNIProxy and exit


Installation
------------

For Debian or Fedora based Linux distributions see building packages below.

**Prerequisites**

+ Autotools (autoconf, automake, gettext and libtool)
+ libev4, libpcre and libudns development headers
+ Perl and cURL for test suite

**Install**

    ./autogen.sh && ./checonfigure --enable-dns && make check && sudo make install

**Building Debian/Ubuntu package**

This is the preferred installation method on recent Debian based distributions:

1. Install required packages

        sudo apt-get install autotools-dev cdbs debhelper dh-autoreconf dpkg-dev gettext libev-dev libpcre3-dev libudns-dev pkg-config fakeroot devscripts

2. Build a Debian package

        ./autogen.sh && dpkg-buildpackage

3. Install the resulting package

        sudo dpkg -i ../sniproxy_<version>_<arch>.deb

**Building Fedora/RedHat package**

This is the preferred installation method for modern Fedora based distributions.

1. Install required packages

        sudo yum install autoconf automake curl gettext-devel libev-devel pcre-devel perl pkgconfig rpm-build udns-devel

2. Build a distribution tarball:

        ./autogen.sh && ./configure --enable-dns && make dist

3. Build a RPM package

        rpmbuild --define "_sourcedir `pwd`" -ba redhat/sniproxy.spec

4. Install resulting RPM

        sudo yum install ../sniproxy-<version>.<arch>.rpm

I've used Scientific Linux 6 a fair amount, but I prefer Debian based
distributions. RPM builds are tested in Travis-CI on Ubuntu, but not natively.
This build process may not follow the current Fedora packaging standards, and
may not even work.

***Building on OS X with Homebrew***

1. install dependencies.

        brew install libev pcre udns autoconf automake gettext libtool

2. Read the warning about gettext and force link it so autogen.sh works. We need the GNU gettext for the macro `AC_LIB_HAVE_LINKFLAGS` which isn't present in the default OS X package.

        brew link --force gettext

3. Make it so

        ./autogen.sh && ./configure --enable-dns && make

OS X support is a best effort, and isn't a primary target platform.


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
        # If port is not specified the listener port will be used
        example.net [2001:DB8::1:10]
        # Or use regular expression to match
        .*\\.com    [2001:DB8::1:11]:443
        # Combining regular expression and wildcard will resolve the hostname
        # client requested and proxy to it
        .*\\.edu    *:443
	# Also possible to rewrite target from source using capture groups
	([a-z]+)\.public\.com   $1.internal.local
    }

DNS Resolution
--------------

Using hostnames or wildcard entries in the configuration requires sniproxy to
be built with [UDNS](http://www.corpit.ru/mjt/udns.html). SNIProxy will still
build without UDNS, but these features will be unavailable.

UDNS uses a single UDP socket for all queries, so it is recommended you use a
local caching DNS resolver (with a single socket each DNS query is protected by
spoofing by a single 16 bit query ID, which makes it relatively easy to spoof).
