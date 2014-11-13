/*
 * Copyright (c) 2014, Dustin Lundquist <dustin@null-ptr.net>
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
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <ev.h>
#include <sys/queue.h>

#include "stats.h"
#include "address.h"
#include "buffer.h"
#include "logger.h"

struct StatsConnection {
    struct ev_io watcher;
    struct Buffer *input;
    struct Buffer *output;
    struct ev_cleanup destructor;

    /* we need to maintain the state of
     * our iteration, since connection list
     * may exceed the size of the output buffer
     *
     * But, a connection or listener could be removed
     * during a stats iteration...
     */
    union {
        struct Connection *connection;
        struct Listener *listener;
    } iter;
};


static void accept_cb(struct ev_loop *, struct ev_io *, int);
static void connection_cb(struct ev_loop *, struct ev_io *, int);
static void free_connection_cb(struct ev_loop *, struct ev_cleanup *, int);
static void free_connection(struct ev_loop *, struct StatsConnection *);
static struct StatsConnection *new_connection(struct ev_loop *);


struct StatsListener *
new_stats_listener() {
    struct StatsListener *listener = calloc(1, sizeof(struct StatsListener));
    if (listener == NULL) {
        err("%s failed to allocate memory for listener", __func__);
        return NULL;
    }

    return listener;
}

static int
init_stats_listener(struct StatsListener *listener, struct ev_loop *loop) {
    const struct sockaddr* sock_addr = address_sa(listener->address);

    if (sock_addr == NULL) {
        err("Failed to parse sock address\n");
        return -1;
    }

    int sockfd = socket(sock_addr->sa_family, SOCK_STREAM, 0);
    if (sockfd < 0) {
        err("%s failed to create socket: %s", __func__, strerror(errno));
        return sockfd;
    }

    int on = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    int result = bind(sockfd, address_sa(listener->address),
            address_sa_len(listener->address));
    if (result < 0) {
        char address[128];
        err("%s failed to bind socket to %s: %s", __func__,
                display_address(listener->address, address, sizeof(address)),
                strerror(errno));
        close(sockfd);
        return result;
    }

    result = listen(sockfd, SOMAXCONN);
    if (result < 0) {
        char address[128];
        err("%s failed to listener on socket to %s: %s", __func__,
                display_address(listener->address, address, sizeof(address)),
                strerror(errno));
        close(sockfd);
        return result;
    }

    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    ev_io_init(&listener->watcher, accept_cb, sockfd, EV_READ);

    listener->watcher.data = listener;

    ev_io_start(loop, &listener->watcher);
    return 1;
}

static void
free_stats_listener(struct StatsListener *listener, struct ev_loop *loop) {
    ev_io_stop(loop, &listener->watcher);

    close(listener->watcher.fd);

    free(listener->address);
    free(listener);
}

static struct StatsConnection *
new_connection(struct ev_loop *loop) {
    struct StatsConnection *connection = malloc(sizeof(struct StatsConnection));
    if (connection == NULL)
        return NULL;

    connection->input = new_buffer(256, loop);
    if (connection->input == NULL) {
        free(connection);
        return NULL;
    }

    connection->output = new_buffer(16 * 1024, loop);
    if (connection->output == NULL) {
        free(connection->input);
        free(connection);
        return NULL;
    }

    ev_cleanup_init(&connection->destructor, free_connection_cb);
    connection->destructor.data = connection;
    ev_cleanup_start(loop, &connection->destructor);

    return connection;
}

static void
accept_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    if (revents & EV_READ) {
        int sockfd = accept(w->fd, NULL, NULL);

        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        struct StatsConnection *connection = new_connection(loop);
        if (connection == NULL) {
            err("%s failed to allocate memory for new stats connection", __func__);
            return;
        }
        ev_io_init(&connection->watcher, connection_cb, sockfd, EV_READ | EV_WRITE);
        connection->watcher.data = connection;

        ev_io_start(loop, &connection->watcher);
    }
}

static void
free_connection_cb(struct ev_loop *loop __attribute__((unused)), struct ev_cleanup *w, int revents __attribute__((unused))) {
    struct StatsConnection *connection = (struct StatsConnection *)w->data;

    notice("%s(%p, %p, %d)\n", __func__, loop, w, revents);

    free_connection(loop, connection);
}

static void
free_connection(struct ev_loop *loop, struct StatsConnection *connection) {
    ev_cleanup_stop(loop, &connection->destructor);

    free(connection->input);
    free(connection->output);
    free(connection);
}

#define IS_TEMPORARY_SOCKERR(_errno) (_errno == EAGAIN || \
                                      _errno == EWOULDBLOCK || \
                                      _errno == EINTR)

static void
connection_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct StatsConnection *connection = (struct StatsConnection *)w->data;

    /* Receive first in case the socket was closed */
    if (revents & EV_READ && buffer_room(connection->input)) {
        ssize_t bytes_received = buffer_recv(connection->input, w->fd, 0, loop);
        if (bytes_received < 0 && !IS_TEMPORARY_SOCKERR(errno)) {
            warn("recv(): %s, closing stats connection",
                    strerror(errno));

            ev_io_stop(loop, &connection->watcher);
            close(w->fd);
            free_connection(loop, connection);
            return;
            revents = 0; /* Clear revents so we don't try to send */
        } else if (bytes_received == 0) { /* peer closed socket */
            ev_io_stop(loop, &connection->watcher);
            close(w->fd);
            free_connection(loop, connection);
            return;
            revents = 0;
        }
    }

    char local_buffer[4096];
    size_t length = snprintf(local_buffer, sizeof(local_buffer), "%s\n", "Hello World");
    buffer_push(connection->output, local_buffer, length);

    // TODO parse request

    /* Transmit */
    if (revents & EV_WRITE && buffer_len(connection->output)) {
        ssize_t bytes_transmitted = buffer_send(connection->output, w->fd, 0, loop);
        if (bytes_transmitted < 0 && !IS_TEMPORARY_SOCKERR(errno)) {
            warn("send(): %s, closing stats connection",
                    strerror(errno));

            ev_io_stop(loop, &connection->watcher);
            close(w->fd);
            free_connection(loop, connection);
            return;
        }
    }

    int events = 0;

    if (buffer_room(connection->input))
        events |= EV_READ;

    if (buffer_len(connection->output))
        events |= EV_WRITE;

    assert(events != 0); // stuck stats connection

    if (events != w->events) {
        ev_io_stop(loop, w);
        ev_io_set(w, w->fd, events);
        ev_io_start(loop, w);
    }
}

int
accept_stats_listener_arg(struct StatsListener *listener, char *arg) {
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

void
free_stats_listeners(struct Stats_head *stats_listeners, struct ev_loop *loop) {
    struct StatsListener *iter;
    char address[128];

    SLIST_FOREACH(iter, stats_listeners, entries) {
        free_stats_listener(iter, loop);
    }
}

void
init_stats_listeners(struct Stats_head *stats_listeners, struct ev_loop *loop) {
    struct StatsListener *iter;
    char address[128];

    SLIST_FOREACH(iter, stats_listeners, entries) {
        if (init_stats_listener(iter, loop) < 0) {
            err("Failed to initialize listener %s",
                    display_address(iter->address, address, sizeof(address)));
            exit(1);
        }
    }
}
