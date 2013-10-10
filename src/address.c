#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h> /* inet_pton */
#include <sys/socket.h>
#include <sys/un.h>
#include <assert.h>
#include "address.h"


struct Address {
    enum {
        HOSTNAME,
        SOCKADDR,
        WILDCARD,
    } type;

    size_t len;
    uint16_t port;
    char data[];
};


static const char valid_label_bytes[] =
"-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";


static struct Address *new_address_sa_port(struct sockaddr *, socklen_t,
        const char *);
static int valid_hostname(const char *);


struct Address *
new_address(const char *hostname_or_ip) {
    struct sockaddr_storage sa;
    char ip_buf[INET6_ADDRSTRLEN];
    char *port;
    size_t len;

    if (hostname_or_ip == NULL)
        return NULL;

    /* Wildcard */
    if (strcmp("*", hostname_or_ip) == 0) {
        struct Address *addr = malloc(sizeof(struct Address));
        if (addr != NULL)
            addr->type = WILDCARD;
        return addr;
    }

    /* [IPv6 address] */
    memset(&sa, 0, sizeof(sa));
    if (hostname_or_ip[0] == '[' && (port = strchr(hostname_or_ip, ']'))) {
        len = port - hostname_or_ip - 1;

        /* inet_pton() will not parse the IP correctly unless it is in a
         * separate string.
         */
        strncpy(ip_buf, hostname_or_ip + 1, len);
        ip_buf[len] = '\0';

        if (inet_pton(AF_INET6, ip_buf,
                    &((struct sockaddr_in6 *)&sa)->sin6_addr) == 1) {
            ((struct sockaddr_in6 *)&sa)->sin6_family = AF_INET6;

            if (port[1] == ':') {
                return new_address_sa_port(
                        (struct sockaddr *)&sa,
                        sizeof(struct sockaddr_in6),
                        port + 2 /* skipping past square bracket colon */);

                return new_address_sa(
                        (struct sockaddr *)&sa,
                        sizeof(struct sockaddr_in6));
            }

            return new_address_sa(
                    (struct sockaddr *)&sa,
                    sizeof(struct sockaddr_in6));
        }
    }

    /* Unix socket */
    memset(&sa, 0, sizeof(sa));
    if (strncmp("unix:", hostname_or_ip, 5) == 0) {
        ((struct sockaddr_un *)&sa)->sun_family = AF_UNIX;
        strncpy(((struct sockaddr_un *)&sa)->sun_path,
                hostname_or_ip + 5, sizeof(sa) -
                offsetof(struct sockaddr_un, sun_path));

        return new_address_sa(
                (struct sockaddr *)&sa, offsetof(struct sockaddr_un, sun_path) +
                strlen(((struct sockaddr_un *)&sa)->sun_path) + 1);
    }

    /* IPv6 address */
    /* we need to test for raw IPv6 address for IPv4 port combinations since a
     * colon would give false positives
     */
    memset(&sa, 0, sizeof(sa));
    if (inet_pton(AF_INET6, hostname_or_ip,
                &((struct sockaddr_in6 *)&sa)->sin6_addr) == 1) {
        ((struct sockaddr_in6 *)&sa)->sin6_family = AF_INET6;

        return new_address_sa(
                (struct sockaddr *)&sa,
                sizeof(struct sockaddr_in6));
    }

    /* IPv4 address with port */
    if ((port = strchr(hostname_or_ip, ':')) != NULL) {
        len = port - hostname_or_ip;

        strncpy(ip_buf, hostname_or_ip, len);
        ip_buf[len] = '\0';

        if (inet_pton(AF_INET, ip_buf,
                    &((struct sockaddr_in *)&sa)->sin_addr) == 1) {
            ((struct sockaddr_in *)&sa)->sin_family = AF_INET;

            return new_address_sa_port(
                    (struct sockaddr *)&sa,
                    sizeof(struct sockaddr_in6),
                    port + 1 /* skipping past colon */);
        }
    }

    /* IPv4 address */
    memset(&sa, 0, sizeof(sa));
    if (inet_pton(AF_INET, hostname_or_ip,
                &((struct sockaddr_in *)&sa)->sin_addr) == 1) {
        ((struct sockaddr_in *)&sa)->sin_family = AF_INET;

        return new_address_sa(
                (struct sockaddr *)&sa,
                sizeof(struct sockaddr_in));
    }

    /* hostname */
    if (valid_hostname(hostname_or_ip)) {
        len = strlen(hostname_or_ip) + 1;
        struct Address *addr = malloc(
                offsetof(struct Address, data) + len);
        if (addr != NULL) {
            addr->type = HOSTNAME;
            addr->port = 0;
            addr->len = strlen(hostname_or_ip);
            memcpy(addr->data, hostname_or_ip, len);
        }

        return addr;
    }

    return NULL;
}

struct Address *
new_address_sa(const struct sockaddr *sa, socklen_t sa_len) {
    struct Address *addr = NULL;

    addr = malloc(offsetof(struct Address, data) + sa_len);
    if (addr != NULL) {
        addr->type = SOCKADDR;
        addr->len = sa_len;
        memcpy(addr->data, sa, sa_len);
    }

    return addr;
}

size_t address_len(const struct Address *addr) {
    switch (addr->type) {
        case HOSTNAME:
            return addr->len +
                offsetof(struct Address, data);
        case SOCKADDR:
            return addr->len +
                offsetof(struct Address, data);
        case WILDCARD:
            return sizeof(struct Address);
        default:
            assert(0);
            return 0;
    }
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

const char *address_hostname(const struct Address *addr) {
    if (addr->type != HOSTNAME)
        return NULL;

    return addr->data;
}

const struct sockaddr *address_sa(const struct Address *addr) {
    if (addr->type != SOCKADDR)
        return NULL;

    return (struct sockaddr *)addr->data;
}

socklen_t address_sa_len(const struct Address *addr) {
    if (addr->type != SOCKADDR)
        return 0;

    return addr->len;
}

int
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
address_set_port(struct Address *addr, int port) {
    if (port < 0 || port > 65535) {
        assert(0);
        return;
    }

    switch (addr->type) {
        case HOSTNAME:
            addr->port = port;
            break;
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
                    /* no op */
                    break;
                default:
                    assert(0);
            }
            break;
        case WILDCARD:
            addr->port = port;
            break;
        default:
            /* invalid Address type */
            assert(0);
    }
}

const char *
display_address(const struct Address *addr, char *buffer, size_t buffer_len) {
    if (addr == NULL || buffer == NULL)
        return NULL;

    switch (addr->type) {
        case HOSTNAME:
            if (addr->port != 0)
                snprintf(buffer, buffer_len, "%s:%d",
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
                snprintf(buffer, buffer_len, "*:%d",
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
                snprintf(buffer, buffer_len, "%s:%d", ip,
                        ntohs(((struct sockaddr_in *)sa)->sin_port));
            else
                snprintf(buffer, buffer_len, "%s", ip);

            break;
        case AF_INET6:
            inet_ntop(AF_INET6,
                    &((const struct sockaddr_in6 *)sa)->sin6_addr,
                    ip, sizeof(ip));

            if (((struct sockaddr_in6 *)sa)->sin6_port != 0)
                snprintf(buffer, buffer_len, "[%s]:%d", ip,
                        ntohs(((struct sockaddr_in6 *)sa)->sin6_port));
            else
                snprintf(buffer, buffer_len, "[%s]", ip);

            break;
        case AF_UNIX:
            snprintf(buffer, buffer_len, "unix:%s",
                    ((struct sockaddr_un *)sa)->sun_path);
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
    int n;

    if (s == NULL || *s == '\0')
        return 0;

    n = strtod(s, &p);

    return n ^ n || /* to suppress unused return value of strtod() */
        *p == '\0'; /* to check entire string was numeric */
}

static struct Address *
new_address_sa_port(struct sockaddr *sa, socklen_t sa_len,
        const char *portstr) {
    if (portstr == NULL)
        return NULL;

    if (!is_numeric(portstr))
        return NULL; /* bad port portion of address */

    int port = atoi(portstr);
    if (port < 0 || port > 65535)
        return NULL;

    struct Address *addr = new_address_sa(sa, sa_len);
    if (addr != NULL)
        address_set_port(addr, port);

    return addr;
}

static int
valid_hostname(const char *hostname) {
    if (hostname == NULL)
        return 0;

    size_t len = strlen(hostname);
    if (len < 1 || len > 255)
        return 0;

    if (hostname[0] == '.')
        return 0;

    for(const char *label = hostname;
            label - 1 != NULL && label[0] != '\0';
            label = strchr(label, '.') + 1) {
        char *next_label = strchr(label, '.');

        if (next_label)
            len = next_label - label;
        else
            len = strlen(label);

        if (len > 63 || len < 1)
            return 0;
        if ((label[0] >= '0' && label[0] <= '9') ||
                label[0] == '-' || label[len - 1] == '-')
            return 0;
        if (strspn(label, valid_label_bytes) < len)
            return 0;
    }

    return 1;
}
