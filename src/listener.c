/*
 * Copyright (c) 2011 and 2012, Dustin Lundquist <dustin@null-ptr.net>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h> /* offsetof */
#include <strings.h> /* strcasecmp() */
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "util.h"
#include "listener.h"
#include "connection.h"
#include "tls.h"
#include "http.h"


static void close_listener(struct ev_loop *, struct Listener *);
static void accept_cb (struct ev_loop *, struct ev_io *, int );
static size_t parse_address(struct sockaddr_storage*, const char*, int);


/*
 * Initialize each listener.
 * Returns an integer pointer to an array of file discriptors allocated
 * terminated by the value -1 or NULL in the case of an error.
 */
int *
init_listeners(struct Listener_head *listeners, const struct Table_head *tables) {
    struct Listener *iter;
    int i = 1; /* include one for terminating -1 */
    int *fd_list = NULL;

    SLIST_FOREACH(iter, listeners, entries)
        i++;

    fd_list = malloc(sizeof(int) * i);
    if (fd_list == NULL) {
        fprintf(stderr, "Failed to malloc\n");
        return NULL;
    }

    i = 0;

    SLIST_FOREACH(iter, listeners, entries) {
        fd_list[i] = init_listener(iter, tables);
        if (fd_list[i] < 0) {
            fprintf(stderr, "Failed to initialize listener #%d -- returned %d: \n", i, fd_list[i]);
            free(fd_list);
            print_listener_config(stderr, iter);
            return NULL;
        }
        i++;
    }
    fd_list[i] = -1; /* sentinal value to mark end of list (0 is valid fd) */

    return fd_list;
}

struct Listener *
new_listener() {
    struct Listener *listener;

    listener = calloc(1, sizeof(struct Listener));
    if (listener == NULL) {
        perror("malloc");
        return NULL;
    }

    listener->protocol = TLS;

    return listener;
}

int
accept_listener_arg(struct Listener *listener, char *arg) {
    if (listener->addr.ss_family == 0) {
        if (isnumeric(arg))
            listener->addr_len = parse_address(&listener->addr, NULL, atoi(arg));
        else
            listener->addr_len = parse_address(&listener->addr, arg, 0);

        if (listener->addr_len == 0) {
            fprintf(stderr, "Invalid listener argument %s\n", arg);
            return -1;
        }
    } else if (listener->addr.ss_family == AF_INET && isnumeric(arg)) {
        ((struct sockaddr_in *)&listener->addr)->sin_port = htons(atoi(arg));
    } else if (listener->addr.ss_family == AF_INET6 && isnumeric(arg)) {
        ((struct sockaddr_in6 *)&listener->addr)->sin6_port = htons(atoi(arg));
    } else {
        fprintf(stderr, "Invalid listener argument %s\n", arg);
    }

    return 1;
}

int
accept_listener_table_name(struct Listener *listener, char *table_name) {
    if (listener->table_name == NULL)
        listener->table_name = strdup(table_name);
    else
        fprintf(stderr, "Duplicate table_name: %s\n", table_name);

    return 1;
}

int
accept_listener_protocol(struct Listener *listener, char *protocol) {
    if (listener->protocol == 0 && strcasecmp(protocol, "http") == 0)
        listener->protocol = HTTP;
    else
        listener->protocol = TLS;

    if (listener->addr.ss_family == AF_INET && ((struct sockaddr_in *)&listener->addr)->sin_port == 0)
        ((struct sockaddr_in *)&listener->addr)->sin_port = listener->protocol == TLS ? 443 : 80;
    else if (listener->addr.ss_family == AF_INET6 && ((struct sockaddr_in6 *)&listener->addr)->sin6_port == 0)
        ((struct sockaddr_in6 *)&listener->addr)->sin6_port = listener->protocol == TLS ? 443 : 80;

    return 1;
}


void
add_listener(struct Listener_head *listeners, struct Listener *listener) {
    SLIST_INSERT_HEAD(listeners, listener, entries);
}

void
remove_listener(struct Listener_head *listeners, struct Listener *listener) {
    SLIST_REMOVE(listeners, listener, Listener, entries);
    close_listener(EV_DEFAULT, listener);
    free_listener(listener);
}

int valid_listener(const struct Listener *listener) {
    union {
        const struct sockaddr_storage *storage;
        const struct sockaddr_in *sin;
        const struct sockaddr_in6 *sin6;
        const struct sockaddr_un *sun;
    } addr;

    addr.storage = &listener->addr;

    switch (addr.storage->ss_family) {
        case AF_UNIX:
            break;
        case AF_INET:
            if (listener->addr_len != sizeof(struct sockaddr_in)) {
                fprintf(stderr, "IPv4 and addr_len not set correctly\n");
                return 0;
            }
            if (addr.sin->sin_port == 0) {
                fprintf(stderr, "IPv4 and port not set\n");
                return 0;
            }
            break;
        case AF_INET6:
            if (listener->addr_len != sizeof(struct sockaddr_in6)) {
                fprintf(stderr, "IPv6 and addr_len not set correctly\n");
                return 0;
            }
            if (addr.sin6->sin6_port == 0) {
                fprintf(stderr, "IPv6 and port not set\n");
                return 0;
            }
            break;
        default:
            fprintf(stderr, "Invalid address family\n");
            return 0;
    }

    if (listener->protocol != TLS && listener->protocol != HTTP) {
        fprintf(stderr, "Invalid protocol\n");
        return 0;
    }

    return 1;
}

int
init_listener(struct Listener *listener, const struct Table_head *tables) {
    int sockfd;
    int on = 1;

    listener->table = lookup_table(tables, listener->table_name);
    if (listener->table == NULL) {
        fprintf(stderr, "Table \"%s\" not defined\n", listener->table_name);
        return -1;
    }

    sockfd = socket(listener->addr.ss_family, SOCK_STREAM, 0);
    if (sockfd < 0) {
        syslog(LOG_CRIT, "socket failed");
        return -2;
    }

    /* set SO_REUSEADDR on server socket to facilitate restart */
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    if (bind(sockfd, (struct sockaddr *)&listener->addr, listener->addr_len) < 0) {
        syslog(LOG_CRIT, "bind failed");
        close(sockfd);
        return -3;
    }

    if (listen(sockfd, SOMAXCONN) < 0) {
        syslog(LOG_CRIT, "listen failed");
        close(sockfd);
        return -4;
    }

    ev_io_init(&listener->rx_watcher, accept_cb, sockfd, EV_READ);
    listener->rx_watcher.data = listener;
    switch (listener->protocol) {
        case TLS:
            listener->parse_packet = parse_tls_header;
            listener->close_client_socket = close_tls_socket;
            break;
        case HTTP:
            listener->parse_packet = parse_http_header;
            listener->close_client_socket = close_http_socket;
            break;
        default:
            syslog(LOG_CRIT, "invalid protocol");
            return -5;
    }

    ev_io_start(EV_DEFAULT, &listener->rx_watcher);

    return sockfd;
}

static void
close_listener(struct ev_loop *loop, struct Listener * listener) {
    ev_io_stop(loop, &listener->rx_watcher);
    close(listener->rx_watcher.fd);
}

void
free_listener(struct Listener *listener) {
    if (listener == NULL)
        return;

    free(listener->table_name);
    free(listener);
}

void
print_listener_config(FILE *file, const struct Listener *listener) {
    char addr_str[INET_ADDRSTRLEN];
    union {
        const struct sockaddr_storage *storage;
        const struct sockaddr_in *sin;
        const struct sockaddr_in6 *sin6;
        const struct sockaddr_un *sun;
    } addr;

    addr.storage = &listener->addr;

    switch (addr.storage->ss_family) {
        case AF_UNIX:
            fprintf(file, "listener unix:%s {\n", (char *)&addr.sun->sun_path);
            break;
        case AF_INET:
            inet_ntop(AF_INET, &addr.sin->sin_addr, addr_str, listener->addr_len);
            fprintf(file, "listener %s %d {\n", addr_str, ntohs(addr.sin->sin_port));
            break;
        case AF_INET6:
            inet_ntop(AF_INET6, &addr.sin6->sin6_addr, addr_str, listener->addr_len);
            fprintf(file, "listener %s %d {\n", addr_str, ntohs(addr.sin6->sin6_port));
            break;
        default:
            fprintf(file, "listener {\n");
            break;
    }

    if (listener->protocol == TLS)
        fprintf(file, "\tprotocol tls\n");
    else
        fprintf(file, "\tprotocol http\n");

    if (listener->table_name)
        fprintf(file, "\ttable %s\n", listener->table_name);

    fprintf(file, "}\n\n");
}

void
free_listeners(struct Listener_head *listeners) {
    struct Listener *iter;

    while ((iter = SLIST_FIRST(listeners)) != NULL) {
        SLIST_REMOVE_HEAD(listeners, entries);
        close_listener(EV_DEFAULT, iter);
        free_listener(iter);
    }
}

static void
accept_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct Listener *listener = (struct Listener *)w->data;
    int sockfd;

    sockfd = accept(listener->rx_watcher.fd, NULL, NULL);
    if (sockfd < 0) {
        syslog(LOG_NOTICE, "accept failed: %s", strerror(errno));
        return;
    }

    add_connection(loop, sockfd, listener);

    return;
}

static size_t
parse_address(struct sockaddr_storage* saddr, const char* address, int port) {
    union {
        struct sockaddr_storage *storage;
        struct sockaddr_in *sin;
        struct sockaddr_in6 *sin6;
        struct sockaddr_un *sun;
    } addr;
    addr.storage = saddr;

    memset(addr.storage, 0, sizeof(struct sockaddr_storage));
    if (address == NULL) {
        addr.sin6->sin6_family = AF_INET6;
        addr.sin6->sin6_port = htons(port);
        return sizeof(struct sockaddr_in6);
    }

    if (inet_pton(AF_INET, address, &addr.sin->sin_addr) == 1) {
        addr.sin->sin_family = AF_INET;
        addr.sin->sin_port = htons(port);
        return sizeof(struct sockaddr_in);
    }

    /* re-zero addr in case inet_pton corrupted it while trying to parse IPv4 */
    memset(addr.storage, 0, sizeof(struct sockaddr_storage));
    if (inet_pton(AF_INET6, address, &addr.sin6->sin6_addr) == 1) {
        addr.sin6->sin6_family = AF_INET6;
        addr.sin6->sin6_port = htons(port);
        return sizeof(struct sockaddr_in6);
    }

    memset(addr.storage, 0, sizeof(struct sockaddr_storage));
    if (strncmp("unix:", address, 5) == 0) {
        addr.sun->sun_family = AF_UNIX;
        strncpy(addr.sun->sun_path, address + 5, sizeof(addr.sun->sun_path));
        return offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun->sun_path);
    }

    return 0;
}
