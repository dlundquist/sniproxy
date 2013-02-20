HTTPS SNI Proxy
===============

Proxies TLS and HTTP connections to separate back end servers based on the
host name in the initial request.

Features
--------
+ Namebased proxying of HTTPS without decrypting traffic. No keys or
  certificates required.
+ Supports TLS and HTTP
+ Supports IPv4, IPv6 and Unix domain sockets for both back end servers and
  listeners.
+ Supports multiple listening sockets per instance.

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

