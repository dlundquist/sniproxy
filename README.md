SNI Proxy
=========

Proxies incoming HTTP and TLS connections based on the hostname contained in
the initial request. This enables HTTPS name based virtual hosting to seperate
backend servers without the installing the private key on the proxy machine. 

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

**Prerequisites**

+ Autotools (autoconf and automake)
+ libpcre development headers
+ Perl and cURL for test suite
    
**Install**

    ./autogen.sh && ./configure && make check && sudo make install


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
