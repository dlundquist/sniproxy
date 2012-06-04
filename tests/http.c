#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "http.h"

const char *good[] = {
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
};
const char *bad[] = {
    "GET / HTTP/1.0\r\n"
        "\r\n",
    "",
    "G",
    "GET ",
    "GET / HTTP/1.0\n"
        "\n",
    "GET / HTTP/1.1\r\n"
        "User-Agent: curl/7.21.0 (x86_64-pc-linux-gnu) libcurl/7.21.0 OpenSSL/0.9.8o zlib/1.2.3.4 libidn/1.18\r\n"
        "Host: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n"
        "Accept: */*\r\n"
        "\r\n",
};

int main() {
    unsigned int i;
    const char *hostname;

    for(i = 0; i < sizeof(good) / sizeof(const char *); i++) {
        hostname = parse_http_header(good[i], strlen(good[i]));

        assert(NULL != hostname);

        assert(0 == strcmp("localhost", hostname));
    }

    for(i = 0; i < sizeof(bad) / sizeof(const char *); i++)
        hostname = parse_http_header(bad[i], strlen(bad[i]));

    return 0;
}

