HTTPS SNI Proxy
===============

Proxies TLS and HTTP requests to backend servers based on SNI
(server name indication) TLS extension.

Features
--------
+ Namebased proxying of HTTPS without decrypting traffic. No keys or certificates required.
+ Also supports HTTP
+ Support IPv4, IPv6 and Unix domain sockets for both backend servers and listeners
+ Multiple listeners per daemon


Usage
-----

    Usage: sni_proxy [-c <config>] [-f]
        -c  configruation file, defaults to /etc/sni_proxy.conf
        -f  run in foreground, do not drop privileges


Installation
------------

    ./autogen.sh && ./configure && make install

Configuration Syntax
--------------------

    user daemon

    listener 127.0.0.1 443 {
        protocol tls
        table "TableName"
    }

    table "TableName" {
        # Match exact request hostnames
        example.com 192.0.2.10      4343
        example.net 2001:DB8::1:10  443
        # Or use PCRE to match
        .*\\.com    2001:DB8::1:11  443
        # Combining PCRE and wildchard will resolve the hostname client requested and proxy to it
        .*\\.edu    *               443
    }

Tests
-----

The included functional\_test expects a local webserver to be available.

