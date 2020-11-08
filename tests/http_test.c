#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "http.h"

static const char *good[] = {
    "GET / HTTP/1.1\r\n"
        "User-Agent: curl/7.21.0 (x86_64-pc-linux-gnu) libcurl/7.21.0 OpenSSL/0.9.8o zlib/1.2.3.4 libidn/1.18\r\n"
        "Host: localhost\r\n"
        "Accept: */*\r\n"
        "\r\n",
    "GET / HTTP/1.1\r\n"
        "User-Agent: curl/7.21.0 (x86_64-pc-linux-gnu) libcurl/7.21.0 OpenSSL/0.9.8o zlib/1.2.3.4 libidn/1.18\r\n"
        "HOST:\t     localhost\r\n"
        "Accept: */*\r\n"
        "\r\n",
    "GET / HTTP/1.1\r\n"
        "User-Agent: curl/7.21.0 (x86_64-pc-linux-gnu) libcurl/7.21.0 OpenSSL/0.9.8o zlib/1.2.3.4 libidn/1.18\r\n"
        "HOST:\t     localhost:8080\r\n"
        "Accept: */*\r\n"
        "\r\n",
    "GET / HTTP/1.1\n"
        "User-Agent: curl/7.21.0 (x86_64-pc-linux-gnu) libcurl/7.21.0 OpenSSL/0.9.8o zlib/1.2.3.4 libidn/1.18\n"
        "Host: localhost\n"
        "Accept: */*\n"
        "\n"
};
static const char *bad[] = {
    "GET / HTTP/1.0\r\n"
        "\r\n",
    "",
    "G",
    "GET ",
    "GET / HTTP/1.0\n"
        "\n",
    "GET / HTTP/1.1\r\n"
        "User-Agent: curl/7.21.0 (x86_64-pc-linux-gnu) libcurl/7.21.0 OpenSSL/0.9.8o zlib/1.2.3.4 libidn/1.18\r\n"
        "Hostname: localhost\r\n"
        "Accept: */*\r\n"
        "\r\n",
    "GET / HTTP/1.1\r\n"
        "User-Agent: curl/7.21.0 (x86_64-pc-linux-gnu) libcurl/7.21.0 OpenSSL/0.9.8o zlib/1.2.3.4 libidn/1.18\r\n"
        "Accept: */*\r\n"
        "\r\n",
};

int main() {
    unsigned int i;
    int result;
    char *hostname;

    for (i = 0; i < sizeof(good) / sizeof(const char *); i++) {
        hostname = NULL;

        result = http_protocol->parse_packet(good[i], strlen(good[i]), &hostname);

        assert(result == 9);

        assert(NULL != hostname);

        assert(0 == strcmp("localhost", hostname));

        free(hostname);
    }

    for (i = 0; i < sizeof(bad) / sizeof(const char *); i++) {
        hostname = NULL;

        result = http_protocol->parse_packet(bad[i], strlen(bad[i]), &hostname);

        assert(result < 0);

        assert(hostname == NULL);
    }

    return 0;
}

