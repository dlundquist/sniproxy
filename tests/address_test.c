#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include "address.h"

struct Test {
    char *input;
    char *output;
    int expected_type;
};

#define TYPE_HOSTNAME 1
#define TYPE_SOCKADDR 2
#define TYPE_WILDCARD 4

static const struct Test good[] = {
    {"www.example.com", "www.example.com", TYPE_HOSTNAME},
    {"localhost", "localhost", TYPE_HOSTNAME},
    {"192.0.2.10", "192.0.2.10", TYPE_SOCKADDR},
    {"192.0.2.10:80", "192.0.2.10:80", TYPE_SOCKADDR},
    {"0.0.0.0", "0.0.0.0", TYPE_SOCKADDR},
    {"0.0.0.0:80", "0.0.0.0:80", TYPE_SOCKADDR},
    {"255.255.255.255:80", "255.255.255.255:80", TYPE_SOCKADDR},
    {"192.0.2.10:65535", "192.0.2.10:65535", TYPE_SOCKADDR},
    {"::", "[::]", TYPE_SOCKADDR},
    {"::1", "[::1]", TYPE_SOCKADDR},
    {"[::]", "[::]", TYPE_SOCKADDR},
    {"[::1]", "[::1]", TYPE_SOCKADDR},
    {"[::]:80", "[::]:80", TYPE_SOCKADDR},
    {"[::]:8080", "[::]:8080", TYPE_SOCKADDR},
    {"2001:db8:0000:0000:0000:0000:0000:0001", "[2001:db8::1]", TYPE_SOCKADDR},
    {"[2001:db8:0000:0000:0000:0000:0000:0001]:65535", "[2001:db8::1]:65535", TYPE_SOCKADDR},
    {"2001:db8::192.0.2.0", "[2001:db8::c000:200]", TYPE_SOCKADDR},
    {"unix:/tmp/foo.sock", "unix:/tmp/foo.sock", TYPE_SOCKADDR},
    {"*", "*", TYPE_WILDCARD}
};
static const char *bad[] = {
    NULL,
    "www..example.com",
    "1n\\/l1|>|-|0$T|\\|4M"
};

int main() {
    /* using volatile variables so we can example core dumps */
    struct Address *addr;
    char buffer[255];

    for (volatile unsigned int i = 0; i < sizeof(good) / sizeof(struct Test); i++) {
        addr = new_address(good[i].input);

        assert(addr != NULL);

        display_address(addr, buffer, sizeof(buffer));

        if (good[i].expected_type & TYPE_HOSTNAME && !address_is_hostname(addr)) {
            fprintf(stderr, "Expected %s to be a hostname\n", buffer);
            return 1;
        }

        if (good[i].expected_type & TYPE_SOCKADDR && !address_is_sockaddr(addr)) {
            fprintf(stderr, "Expected %s to be a sockaddr\n", buffer);
            return 1;
        }

        if (good[i].expected_type & TYPE_WILDCARD && !address_is_wildcard(addr)) {
            fprintf(stderr, "Expected %s to be a wildcard\n", buffer);
            return 1;
        }


        if (strcmp(buffer, good[i].output)) {
            fprintf(stderr, "display_address() returned \"%s\", expected \"%s\"\n", buffer, good[i].output);
            return 1;
        }

        free(addr);
    }

    for (volatile unsigned int i = 0; i < sizeof(bad) / sizeof(const char *); i++) {
        addr = new_address(bad[i]);

        if (addr != NULL) {
            fprintf(stderr, "Accepted bad hostname \"%s\"\n", bad[i]);
            return 1;
        }
    }

    return 0;
}

