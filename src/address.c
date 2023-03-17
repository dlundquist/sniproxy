/*
 * Copyright (c) 2013, Dustin Lundquist <dustin@null-ptr.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> /* tolower */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> /* inet_pton */
#include <sys/un.h>
#include <assert.h>
#include "address.h"


struct Address {
    enum {
        HOSTNAME,
        SOCKADDR,
        WILDCARD,
    } type;

    size_t len;     /* length of data */
    uint16_t port;  /* for hostname and wildcard */
    char data[];
};


static const char valid_label_bytes[] =
"-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";


#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))


static int valid_hostname(const char *);


struct Address *
new_address(const char *hostname_or_ip) {
    union {
        struct sockaddr a;
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
        struct sockaddr_un un;
        struct sockaddr_storage s;
    } s;
    char ip_buf[ADDRESS_BUFFER_SIZE];
    char *port;
    size_t len;

    if (hostname_or_ip == NULL)
        return NULL;

    /* IPv6 address */
    /* we need to test for raw IPv6 address for IPv4 port combinations since a
     * colon would give false positives
     */
    memset(&s, 0, sizeof(s));
    if (inet_pton(AF_INET6, hostname_or_ip,
                &s.in6.sin6_addr) == 1) {
        s.in6.sin6_family = AF_INET6;

        return new_address_sa(&s.a, sizeof(s.in6));
    }

    /* Unix socket */
    memset(&s, 0, sizeof(s));
    if (strncmp("unix:", hostname_or_ip, 5) == 0) {
        if (strlen(hostname_or_ip) >=
                sizeof(s.un.sun_path))
            return NULL;

        /* XXX: only supporting pathname unix sockets */
        s.un.sun_family = AF_UNIX;
        strncpy(s.un.sun_path,
                hostname_or_ip + 5,
                sizeof(s.un.sun_path) - 1);

        return new_address_sa(&s.a, offsetof(struct sockaddr_un, sun_path) +
                              strlen(s.un.sun_path) + 1);
    }

    /* Trailing port */
    if ((port = strrchr(hostname_or_ip, ':')) != NULL &&
            is_numeric(port + 1)) {
        len = (size_t)(port - hostname_or_ip);
        int port_num = atoi(port + 1);

        if (len < sizeof(ip_buf) && port_num >= 0 && port_num <= 65535) {
            strncpy(ip_buf, hostname_or_ip, len);
            ip_buf[len] = '\0';

            struct Address *addr = new_address(ip_buf);
            if (addr != NULL)
                address_set_port(addr, (uint16_t) port_num);

            return addr;
        }
    }

    /* Wildcard */
    if (strcmp("*", hostname_or_ip) == 0) {
        struct Address *addr = malloc(sizeof(struct Address));
        if (addr != NULL) {
            addr->type = WILDCARD;
            addr->len = 0;
            address_set_port(addr, 0);
        }
        return addr;
    }

    /* IPv4 address */
    memset(&s, 0, sizeof(s));
    if (inet_pton(AF_INET, hostname_or_ip,
                  &s.in.sin_addr) == 1) {
        s.in.sin_family = AF_INET;

        return new_address_sa(&s.a, sizeof(s.in));
    }

    /* [IPv6 address] */
    memset(&s, 0, sizeof(s));
    if (hostname_or_ip[0] == '[' &&
            (port = strchr(hostname_or_ip, ']')) != NULL) {
        len = (size_t)(port - hostname_or_ip - 1);
        if (len >= INET6_ADDRSTRLEN)
            return NULL;

        /* inet_pton() will not parse the IP correctly unless it is in a
         * separate string.
         */
        strncpy(ip_buf, hostname_or_ip + 1, len);
        ip_buf[len] = '\0';

        if (inet_pton(AF_INET6, ip_buf,
                      &s.in6.sin6_addr) == 1) {
            s.in6.sin6_family = AF_INET6;

            return new_address_sa(&s.a, sizeof(s.in6));
        }
    }

    /* hostname */
    if (valid_hostname(hostname_or_ip)) {
        len = strlen(hostname_or_ip);
        struct Address *addr = malloc(
                offsetof(struct Address, data) + len + 1);
        if (addr != NULL) {
            addr->type = HOSTNAME;
            addr->port = 0;
            addr->len = len;
            memcpy(addr->data, hostname_or_ip, len);
            addr->data[addr->len] = '\0';

            /* Store address in lower case */
            for (char *c = addr->data; *c != '\0'; c++)
                *c = tolower(*c);
        }

        return addr;
    }

    return NULL;
}

struct Address *
new_address_sa(const struct sockaddr *sa, socklen_t sa_len) {
    struct Address *addr = malloc(offsetof(struct Address, data) + sa_len);
    if (addr != NULL) {
        addr->type = SOCKADDR;
        addr->len = sa_len;
        memcpy(addr->data, sa, sa_len);
        addr->port = address_port(addr);
    }

    return addr;
}

struct Address *
copy_address(const struct Address *addr) {
    size_t len = address_len(addr);
    struct Address *new_addr = malloc(len);

    if (new_addr != NULL)
        memcpy(new_addr, addr, len);

    return new_addr;
}

size_t
address_len(const struct Address *addr) {
    switch (addr->type) {
        case HOSTNAME:
            /* include trailing null byte */
            return offsetof(struct Address, data) + addr->len + 1;
        case SOCKADDR:
            return offsetof(struct Address, data) + addr->len;
        case WILDCARD:
            return sizeof(struct Address);
        default:
            assert(0);
            return 0;
    }
}

int
address_compare(const struct Address *addr_1, const struct Address *addr_2) {
    if (addr_1 == NULL && addr_2 == NULL)
        return 0;
    if (addr_1 == NULL && addr_2 != NULL)
        return -1;
    if (addr_1 != NULL && addr_2 == NULL)
        return 1;

    if (addr_1->type < addr_2->type)
        return -1;
    if (addr_1->type > addr_2->type)
        return 1;

    size_t addr1_len = addr_1->len;
    size_t addr2_len = addr_2->len;
    int result = memcmp(addr_1->data, addr_2->data, MIN(addr1_len, addr2_len));

    if (result == 0) { /* they match, find a tie breaker */
        if (addr1_len < addr2_len)
            return -1;
        if (addr1_len > addr2_len)
            return 1;

        if (addr_1->port < addr_2->port)
            return -1;
        if (addr_1->port > addr_2->port)
            return 1;
    }

    return result;
}

int
address_is_hostname(const struct Address *addr) {
    return addr != NULL && addr->type == HOSTNAME;
}

int
address_is_sockaddr(const struct Address *addr) {
    return addr != NULL && addr->type == SOCKADDR;
}

int
address_is_wildcard(const struct Address *addr) {
    return addr != NULL && addr->type == WILDCARD;
}

const char *
address_hostname(const struct Address *addr) {
    if (addr->type != HOSTNAME)
        return NULL;

    return addr->data;
}

const struct sockaddr *
address_sa(const struct Address *addr) {
    if (addr->type != SOCKADDR)
        return NULL;

    return (struct sockaddr *)addr->data;
}

socklen_t
address_sa_len(const struct Address *addr) {
    if (addr->type != SOCKADDR)
        return 0;

    return addr->len;
}

uint16_t
address_port(const struct Address *addr) {
    switch (addr->type) {
        case HOSTNAME:
            return addr->port;
        case SOCKADDR:
            switch (address_sa(addr)->sa_family) {
                case AF_INET:
                    return ntohs(((struct sockaddr_in *)addr->data)
                            ->sin_port);
                case AF_INET6:
                    return ntohs(((struct sockaddr_in6 *)addr->data)
                            ->sin6_port);
                case AF_UNIX:
                case AF_UNSPEC:
                    return 0;
                default:
                    assert(0);
                    return 0;
            }
        case WILDCARD:
            return addr->port;
        default:
            /* invalid Address type */
            assert(0);
            return 0;
    }
}

void
address_set_port(struct Address *addr, uint16_t port) {
    switch (addr->type) {
        case SOCKADDR:
            switch (address_sa(addr)->sa_family) {
                case AF_INET:
                    (((struct sockaddr_in *)addr->data) ->sin_port) =
                        htons(port);
                    break;
                case AF_INET6:
                    (((struct sockaddr_in6 *)addr->data) ->sin6_port) =
                        htons(port);
                    break;
                case AF_UNIX:
                case AF_UNSPEC:
                    /* no op */
                    break;
                default:
                    assert(0);
            }
            /* fall through */
        case HOSTNAME:
        case WILDCARD:
            addr->port = port;
            break;
        default:
            /* invalid Address type */
            assert(0);
    }
}

int
address_set_port_str(struct Address *addr, const char* str) {
    int port = atoi(str);
    if (port < 0 || port > 65535) {
        return 0;
    }
    address_set_port(addr, (uint16_t)port);
    return 1;
}

const char *
display_address(const struct Address *addr, char *buffer, size_t buffer_len) {
    if (addr == NULL || buffer == NULL)
        return NULL;

    switch (addr->type) {
        case HOSTNAME:
            if (addr->port != 0)
                snprintf(buffer, buffer_len, "%s:%" PRIu16,
                        addr->data,
                        addr->port);
            else
                snprintf(buffer, buffer_len, "%s",
                        addr->data);
            return buffer;
        case SOCKADDR:
            return display_sockaddr(addr->data, buffer, buffer_len);
        case WILDCARD:
            if (addr->port != 0)
                snprintf(buffer, buffer_len, "*:%" PRIu16,
                        addr->port);
            else
                snprintf(buffer, buffer_len, "*");
            return buffer;
        default:
            assert(0);
            return NULL;
    }
}

const char *
display_sockaddr(const void *sa, char *buffer, size_t buffer_len) {
    char ip[INET6_ADDRSTRLEN];
    if (sa == NULL || buffer == NULL)
        return NULL;

    switch (((const struct sockaddr *)sa)->sa_family) {
        case AF_INET:
            inet_ntop(AF_INET,
                      &((const struct sockaddr_in *)sa)->sin_addr,
                      ip, sizeof(ip));

            if (((struct sockaddr_in *)sa)->sin_port != 0)
                snprintf(buffer, buffer_len, "%s:%" PRIu16, ip,
                        ntohs(((struct sockaddr_in *)sa)->sin_port));
            else
                snprintf(buffer, buffer_len, "%s", ip);

            break;
        case AF_INET6:
            inet_ntop(AF_INET6,
                      &((const struct sockaddr_in6 *)sa)->sin6_addr,
                      ip, sizeof(ip));

            if (((struct sockaddr_in6 *)sa)->sin6_port != 0)
                snprintf(buffer, buffer_len, "[%s]:%" PRIu16, ip,
                         ntohs(((struct sockaddr_in6 *)sa)->sin6_port));
            else
                snprintf(buffer, buffer_len, "[%s]", ip);

            break;
        case AF_UNIX:
            snprintf(buffer, buffer_len, "unix:%s",
                     ((struct sockaddr_un *)sa)->sun_path);
            break;
        case AF_UNSPEC:
            snprintf(buffer, buffer_len, "NONE");
            break;
        default:
            /* unexpected AF */
            assert(0);
    }
    return buffer;
}

int
is_numeric(const char *s) {
    char *p;

    if (s == NULL || *s == '\0')
        return 0;

    int n = strtod(s, &p);
    (void)n; /* unused */

    return *p == '\0'; /* entire string was numeric */
}

static int
valid_hostname(const char *hostname) {
    if (hostname == NULL)
        return 0;

    size_t hostname_len = strlen(hostname);
    if (hostname_len < 1 || hostname_len > 255)
        return 0;

    if (hostname[0] == '.')
        return 0;

    const char *hostname_end = hostname + hostname_len;
    for (const char *label = hostname; label < hostname_end;) {
        size_t label_len = (size_t)(hostname_end - label);
        char *next_dot = strchr(label, '.');
        if (next_dot != NULL)
            label_len = (size_t)(next_dot - label);
        assert(label + label_len <= hostname_end);

        if (label_len > 63 || label_len < 1)
            return 0;

        if (label[0] == '-' || label[label_len - 1] == '-')
            return 0;

        if (strspn(label, valid_label_bytes) < label_len)
            return 0;

        label += label_len + 1;
    }

    return 1;
}
