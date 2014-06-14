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
#include <fcntl.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include "address.h"
#include "listener.h"
#include "connection.h"
#include "logger.h"
#include "protocol.h"
#include "tls.h"
#include "http.h"


static void close_listener(struct ev_loop *, struct Listener *);
static void accept_cb(struct ev_loop *, struct ev_io *, int);
static void backoff_timer_cb(struct ev_loop *, struct ev_timer *, int);


/*
 * Initialize each listener.
 */
void
init_listeners(struct Listener_head *listeners,
        const struct Table_head *tables) {
    struct Listener *iter;

    SLIST_FOREACH(iter, listeners, entries) {
        if (init_listener(iter, tables) < 0) {
            err("Failed to initialize listener");
            print_listener_config(stderr, iter);
            exit(1);
        }
    }
}

struct Listener *
new_listener() {
    struct Listener *listener;

    listener = calloc(1, sizeof(struct Listener));
    if (listener == NULL) {
        err("calloc");
        return NULL;
    }

    listener->address = NULL;
    listener->fallback_address = NULL;
    listener->source_address = NULL;
    listener->protocol = tls_protocol;
    listener->access_log = NULL;
    listener->log_bad_requests = 0;

    return listener;
}

int
accept_listener_arg(struct Listener *listener, char *arg) {
    if (listener->address == NULL && !is_numeric(arg)) {
        listener->address = new_address(arg);

        if (listener->address == NULL ||
                !address_is_sockaddr(listener->address)) {
            err("Invalid listener argument %s", arg);
            return -1;
        }
    } else if (listener->address == NULL && is_numeric(arg)) {
        listener->address = new_address("[::]");

        if (listener->address == NULL ||
                !address_is_sockaddr(listener->address)) {
            err("Unable to initialize default address");
            return -1;
        }

        address_set_port(listener->address, atoi(arg));
    } else if (address_port(listener->address) == 0 && is_numeric(arg)) {
        address_set_port(listener->address, atoi(arg));
    } else {
        err("Invalid listener argument %s", arg);
    }

    return 1;
}

int
accept_listener_table_name(struct Listener *listener, char *table_name) {
    if (listener->table_name == NULL)
        listener->table_name = strdup(table_name);
    else
        err("Duplicate table_name: %s", table_name);

    return 1;
}

int
accept_listener_protocol(struct Listener *listener, char *protocol) {
    if (strncasecmp(protocol, http_protocol->name, strlen(protocol)) == 0)
        listener->protocol = http_protocol;
    else
        listener->protocol = tls_protocol;

    if (address_port(listener->address) == 0)
        address_set_port(listener->address, listener->protocol->default_port);

    return 1;
}

int
accept_listener_fallback_address(struct Listener *listener, char *fallback) {
    if (listener->fallback_address != NULL) {
        err("Duplicate fallback address: %s", fallback);
        return 0;
    }
    listener->fallback_address = new_address(fallback);
    if (listener->fallback_address == NULL) {
        err("Unable to parse fallback address: %s", fallback);
        return 0;
    }
#ifndef HAVE_LIBUDNS
    if (!address_is_sockaddr(listener->fallback_address)) {
        err("Only fallback socket addresses permitted when compiled without libudns");
        free(listener->fallback_address);
        listener->fallback_address = NULL;
        return 0;
    }
#endif
    if (address_is_wildcard(listener->fallback_address)) {
        free(listener->fallback_address);
        listener->fallback_address = NULL;
        /* The wildcard functionality requires successfully parsing the
         * hostname from the client's request, if we couldn't find the
         * hostname and are using a fallback address it doesn't make
         * much sense to configure it as a wildcard. */
        err("Wildcard address prohibited as fallback address");
        return 0;
    }

    return 1;
}

int
accept_listener_source_address(struct Listener *listener, char *source) {
    if (listener->source_address != NULL) {
        err("Duplicate source address: %s", source);
        return 0;
    }

    listener->source_address = new_address(source);
    if (listener->source_address == NULL) {
        err("Unable to parse source address: %s", source);
        return 0;
    }
    if (!address_is_sockaddr(listener->source_address)) {
        err("Only source socket addresses permitted");
        free(listener->source_address);
        listener->source_address = NULL;
        return 0;
    }
    if (address_port(listener->source_address) != 0) {
        char address[256];
        err("Source address on listener %s set to non zero port, "
                "this prevents multiple connection to each backend server.",
                display_address(listener->address, address, sizeof(address)));
    }

    return 1;
}

int
accept_listener_bad_request_action(struct Listener *listener, char *action) {
    if (strncmp("log", action, strlen(action)) == 0) {
        listener->log_bad_requests = 1;
    }

    return 1;
}

/*
 * Insert an additional listener in to the sorted list of listeners
 */
void
add_listener(struct Listener_head *listeners, struct Listener *listener) {
    assert(listeners != NULL);
    assert(listener != NULL);
    assert(listener->address != NULL);

    if (SLIST_FIRST(listeners) == NULL ||
            address_compare(listener->address, SLIST_FIRST(listeners)->address) < 0) {
        SLIST_INSERT_HEAD(listeners, listener, entries);
        return;
    }

    struct Listener *iter;
    SLIST_FOREACH(iter, listeners, entries) {
        if (SLIST_NEXT(iter, entries) == NULL ||
                address_compare(listener->address, SLIST_NEXT(iter, entries)->address) < 0) {
            SLIST_INSERT_AFTER(iter, listener, entries);
            return;
        }
    }
}

void
remove_listener(struct Listener_head *listeners, struct Listener *listener) {
    SLIST_REMOVE(listeners, listener, Listener, entries);
    close_listener(EV_DEFAULT, listener);
    free_listener(listener);
}

int
valid_listener(const struct Listener *listener) {
    if (listener->address == NULL) {
        err("No address specified");
        return 0;
    }

    if (!address_is_sockaddr(listener->address)) {
        err("Address not specified as IP/socket");
        return 0;
    }

    switch (address_sa(listener->address)->sa_family) {
        case AF_UNIX:
            break;
        case AF_INET:
            /* fall through */
        case AF_INET6:
            if (address_port(listener->address) == 0) {
                err("No port specified");
                return 0;
            }
            break;
        default:
            err("Invalid address family");
            return 0;
    }

    if (listener->protocol != tls_protocol && listener->protocol != http_protocol) {
        err("Invalid protocol");
        return 0;
    }

    return 1;
}

int
init_listener(struct Listener *listener, const struct Table_head *tables) {
    int sockfd;
    int on = 1;

    listener->table = table_lookup(tables, listener->table_name);
    if (listener->table == NULL) {
        err("Table \"%s\" not defined", listener->table_name);
        return -1;
    }
    init_table(listener->table);

    /* If no port was specified on the fallback address, inherit the address
     * from the listening address */
    if (listener->fallback_address &&
            address_port(listener->fallback_address) == 0)
        address_set_port(listener->fallback_address,
                address_port(listener->address));

    sockfd = socket(address_sa(listener->address)->sa_family, SOCK_STREAM, 0);
    if (sockfd < 0) {
        err("socket failed: %s", strerror(errno));
        return -2;
    }

    /* set SO_REUSEADDR on server socket to facilitate restart */
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    if (bind(sockfd, address_sa(listener->address),
                address_sa_len(listener->address)) < 0) {
        err("bind failed: %s", strerror(errno));
        close(sockfd);
        return -3;
    }

    if (listen(sockfd, SOMAXCONN) < 0) {
        err("listen failed: %s", strerror(errno));
        close(sockfd);
        return -4;
    }


    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    ev_io_init(&listener->watcher, accept_cb, sockfd, EV_READ);
    listener->watcher.data = listener;
    ev_timer_init(&listener->backoff_timer, backoff_timer_cb, 0.0, 0.0);
    listener->backoff_timer.data = listener;

    ev_io_start(EV_DEFAULT, &listener->watcher);

    return sockfd;
}

struct Address *
listener_lookup_server_address(const struct Listener *listener,
        const char *name, size_t name_len) {
    struct Address *new_addr = NULL;
    const struct Address *addr =
        table_lookup_server_address(listener->table, name, name_len);

    if (addr == NULL)
        addr = listener->fallback_address;

    if (addr == NULL)
        return NULL;

    int port = address_port(addr);

    if (address_is_wildcard(addr)) {
        new_addr = new_address(name);
        if (new_addr == NULL) {
            warn("Invalid hostname %.*s", (int)name_len, name);

            return listener->fallback_address;
        }

        if (port != 0)
            address_set_port(new_addr, port);
    } else {
        size_t len = address_len(addr);
        new_addr = malloc(len);
        if (new_addr == NULL) {
            err("%s: malloc", __func__);

            return listener->fallback_address;
        }

        memcpy(new_addr, addr, len);
    }

    if (port == 0)
        address_set_port(new_addr, address_port(listener->address));

    return new_addr;
}

void
print_listener_config(FILE *file, const struct Listener *listener) {
    char address[256];

    fprintf(file, "listener %s {\n",
            display_address(listener->address, address, sizeof(address)));

    fprintf(file, "\tprotocol %s\n", listener->protocol->name);

    if (listener->table_name)
        fprintf(file, "\ttable %s\n", listener->table_name);

    if (listener->fallback_address)
        fprintf(file, "\tfallback %s\n",
                display_address(listener->fallback_address,
                    address, sizeof(address)));

    if (listener->source_address)
        fprintf(file, "\tsource %s\n",
                display_address(listener->source_address,
                    address, sizeof(address)));

    fprintf(file, "}\n\n");
}

static void
close_listener(struct ev_loop *loop, struct Listener * listener) {
    ev_io_stop(loop, &listener->watcher);
    close(listener->watcher.fd);
}

void
free_listener(struct Listener *listener) {
    if (listener == NULL)
        return;

    free(listener->address);
    free(listener->fallback_address);
    free(listener->source_address);
    free(listener->table_name);
    free_logger(listener->access_log);
    free(listener);
}

void
free_listeners(struct Listener_head *listeners) {
    struct Listener *iter;

    while ((iter = SLIST_FIRST(listeners)) != NULL)
        remove_listener(listeners, iter);
}

static void
accept_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct Listener *listener = (struct Listener *)w->data;

    if (revents & EV_READ) {
        int result = accept_connection(listener, loop);
        if (result == 0 && (errno == EMFILE || errno == ENFILE)) {
            char address_buf[256];
            int backoff_time = 2;

            err("File descriptor limit reached! "
                "Suspending accepting new connections on %s for %d seconds",
                display_address(listener->address, address_buf, sizeof(address_buf)),
                backoff_time);
            ev_io_stop(loop, w);

            ev_timer_set(&listener->backoff_timer, backoff_time, 0.0);
            ev_timer_start(loop, &listener->backoff_timer);
        }
    }
}

static void
backoff_timer_cb(struct ev_loop *loop, struct ev_timer *w, int revents) {
    struct Listener *listener = (struct Listener *)w->data;

    if (revents & EV_TIMER) {
        ev_timer_stop(loop, &listener->backoff_timer);

        ev_io_set(&listener->watcher, listener->watcher.fd, EV_READ);
        ev_io_start(loop, &listener->watcher);
    }
}
