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
    struct ProtocolRes res;
    struct Listener l;

    for (i = 0; i < sizeof(good) / sizeof(const char *); i++) {
        memset(&res, 0, sizeof(res));

        result = http_protocol->parse_packet(&l, good[i], strlen(good[i]), &res);

        assert(result == 9);

        assert(NULL != res.name);

        assert(1 == res.name_type);

        assert(0 == strcmp("localhost", res.name));

        free(res.name);
    }

    for (i = 0; i < sizeof(bad) / sizeof(const char *); i++) {
        memset(&res, 0, sizeof(res));

        result = http_protocol->parse_packet(&l, bad[i], strlen(bad[i]), &res);

        assert(result < 0);

        assert(res.name == NULL);
    }

    return 0;
}

