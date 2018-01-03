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
    int port;
};

#define TYPE_HOSTNAME 1
#define TYPE_SOCKADDR 2
#define TYPE_WILDCARD 4

static const struct Test good[] = {
    {"www.example.com", "www.example.com", TYPE_HOSTNAME, 0},
    {"www.example.com:80", "www.example.com:80", TYPE_HOSTNAME, 80},
    {"hyphens-are-permited.example.com", "hyphens-are-permited.example.com", TYPE_HOSTNAME, 0},
    {"79423.all-numeric-labels-are-permitted.com", "79423.all-numeric-labels-are-permitted.com", TYPE_HOSTNAME, 0},
    {"localhost", "localhost", TYPE_HOSTNAME, 0},
    {"192.0.2.10", "192.0.2.10", TYPE_SOCKADDR, 0},
    {"192.0.2.10:80", "192.0.2.10:80", TYPE_SOCKADDR, 80},
    {"0.0.0.0", "0.0.0.0", TYPE_SOCKADDR, 0},
    {"0.0.0.0:80", "0.0.0.0:80", TYPE_SOCKADDR, 80},
    {"255.255.255.255:80", "255.255.255.255:80", TYPE_SOCKADDR, 80},
    {"192.0.2.10:65535", "192.0.2.10:65535", TYPE_SOCKADDR, 65535},
    {"::", "[::]", TYPE_SOCKADDR, 0},
    {"::1", "[::1]", TYPE_SOCKADDR, 0},
    {"[::]", "[::]", TYPE_SOCKADDR, 0},
    {"[::1]", "[::1]", TYPE_SOCKADDR, 0},
    {"[::]:80", "[::]:80", TYPE_SOCKADDR, 80},
    {"[::]:8080", "[::]:8080", TYPE_SOCKADDR, 8080},
    {"2001:db8:0000:0000:0000:0000:0000:0001", "[2001:db8::1]", TYPE_SOCKADDR, 0},
    {"[2001:db8:0000:0000:0000:0000:0000:0001]:65535", "[2001:db8::1]:65535", TYPE_SOCKADDR, 65535},
    {"2001:db8::192.0.2.0", "[2001:db8::c000:200]", TYPE_SOCKADDR, 0},
    {"unix:/tmp/foo.sock", "unix:/tmp/foo.sock", TYPE_SOCKADDR, 0},
    {"*", "*", TYPE_WILDCARD, 0},
    {"*:80", "*:80", TYPE_WILDCARD, 80},
    /* Please don't do this (add the port to the end of an IPv6 address) */
    {"2001:db8:0:0:0:0:0:1:80", "[2001:db8::1]:80", TYPE_SOCKADDR, 80}
};
static const char *bad[] = {
    NULL,
    "www..example.com",
    "-www.example.com",
    "1n\\/l1|>|-|0$T|\\|4M"
};

int compare_address_strings(const char *a, const char *b) {
    struct Address *addr_a = new_address(a);
    struct Address *addr_b = new_address(b);

    int result = address_compare(addr_a, addr_b);

    free(addr_a);
    free(addr_b);

    return result;
}

int main() {
    /* using volatile variables so we can examine core dumps */
    for (volatile unsigned int i = 0; i < sizeof(good) / sizeof(struct Test); i++) {
        int port;
        char buffer[255];
        struct Address *addr = new_address(good[i].input);

        assert(addr != NULL);
        assert(address_compare(addr, addr) == 0);
        assert(address_compare(NULL, addr) < 0);
        assert(address_compare(addr, NULL) > 0);
        assert(address_len(addr) > 0);

        if (good[i].expected_type & TYPE_HOSTNAME) {
            assert(address_is_hostname(addr));
            assert(!address_is_sockaddr(addr));
            assert(!address_is_wildcard(addr));
            assert(address_hostname(addr) != NULL);
            assert(address_sa(addr) == NULL);
            assert(address_sa_len(addr) == 0);
        } else if (good[i].expected_type & TYPE_SOCKADDR) {
            assert(!address_is_hostname(addr));
            assert(address_is_sockaddr(addr));
            assert(!address_is_wildcard(addr));
            assert(address_hostname(addr) == NULL);
            assert(address_sa(addr) != NULL);
            assert(address_sa_len(addr) > 0);
        } else if (good[i].expected_type & TYPE_WILDCARD) {
            assert(!address_is_hostname(addr));
            assert(!address_is_sockaddr(addr));
            assert(address_is_wildcard(addr));
            assert(address_hostname(addr) == NULL);
            assert(address_sa(addr) == NULL);
            assert(address_sa_len(addr) == 0);
        }

        display_address(addr, buffer, sizeof(buffer));

        if (strcmp(buffer, good[i].output)) {
            fprintf(stderr, "display_address(%p) returned \"%s\", expected \"%s\"\n", (void *)addr, buffer, good[i].output);
            return 1;
        }

        assert(display_address(addr, NULL, 0) == NULL);

        port = address_port(addr);

        if (good[i].port != port) {
            fprintf(stderr, "address_port(%p) return %d, expected %d\n", (void *)addr, port, good[i].port);
            return 1;
        }

        address_set_port(addr, port);

        if (good[i].port != port) {
            fprintf(stderr, "address_port(%p) return %d, expected %d\n", (void *)addr, port, good[i].port);
            return 1;
        }

        free(addr);
    }

    for (volatile unsigned int i = 0; i < sizeof(bad) / sizeof(const char *); i++) {
        struct Address *addr = new_address(bad[i]);

        if (addr != NULL) {
            fprintf(stderr, "Accepted bad hostname \"%s\"\n", bad[i]);
            return 1;
        }
    }

    assert(compare_address_strings("unix:/dev/log", "127.0.0.1") < 0);
    assert(compare_address_strings("unix:/dev/log", "unix:/dev/logsocket") < 0);
    assert(compare_address_strings("example.co", "example.com") != 0);
    assert(compare_address_strings("0.0.0.0", "127.0.0.1") < 0);
    assert(compare_address_strings("127.0.0.1", "0.0.0.0") > 0);
    assert(compare_address_strings("127.0.0.1", "127.0.0.1") == 0);
    assert(compare_address_strings("127.0.0.1:80", "127.0.0.1:81") < 0);
    assert(compare_address_strings("*:80", "*:81") < 0);
    assert(compare_address_strings("*:81", "*:80") > 0);
    assert(compare_address_strings("example.com", "example.net") < 0);
    assert(compare_address_strings("example.net", "example.com") > 0);
    assert(compare_address_strings("example.com", "example.com.net") < 0);
    assert(compare_address_strings("example.com.net", "example.com") > 0);
    assert(compare_address_strings("example.com", "example.com:80") < 0);
    assert(compare_address_strings("example.com:80", "example.com") > 0);
    assert(compare_address_strings(NULL, "example.com") < 0);
    assert(compare_address_strings("example.com", NULL) > 0);
    assert(compare_address_strings("example.com", "::") < 0);
    assert(compare_address_strings("::", "example.com") > 0);
    assert(compare_address_strings("0.0.0.0", "*") < 0);
    assert(compare_address_strings("*", "0.0.0.0") > 0);

    do {
        struct Address *addr = new_address("*");

        assert(addr != NULL);
        assert(address_len(addr) > 0);

        free(addr);
    } while (0);

    return 0;
}

