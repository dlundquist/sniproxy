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
#include "stats.h"
#include "address.h"
#include "buffer.h"
#include "logger.h"


struct StatsListener {
    struct Address *address;
    struct ev_io watcher;
};

struct StatsConnection {
    struct ev_io watcher;
    struct Buffer *input;
    struct Buffer *output;

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


struct StatsListener *
new_stats_listener(const char *address) {
    struct StatsListener *listener = malloc(sizeof(struct StatsListener));
    if (listener == NULL) {
        err("%s failed to allocate memory for listener", __func__);
        return NULL;
    }

    listener->address = new_address(address);
    if (listener->address == NULL) {
        err("%s invalid address", __func__);
        free(listener);
        return NULL;
    }

    return listener;
}

void
init_stats_listener(struct StatsListener *listener, struct ev_loop *loop) {
    int sockfd = socket(address_sa(listener->address)->sa_family, SOCK_STREAM, 0);
    if (sockfd < 0) {
        err("%s failed to create socket: %s", __func__, strerror(errno));
        return;
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
        return;
    }

    listen(sockfd, SOMAXCONN);
    if (result < 0) {
        char address[128];
        err("%s failed to listener on socket to %s: %s", __func__,
                display_address(listener->address, address, sizeof(address)),
                strerror(errno));
        close(sockfd);
        return;
    }

    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    ev_io_init(&listener->watcher, accept_cb, sockfd, EV_READ);
    listener->watcher.data = listener;

    ev_io_start(loop, &listener->watcher);
}

void
free_stats_listener(struct StatsListener *listener, struct ev_loop *loop) {
    ev_io_stop(loop, &listener->watcher);

    close(listener->watcher.fd);
    free(listener);
}

static void
accept_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    if (revents & EV_READ) {
        int sockfd = accept(w->fd, NULL, NULL);

        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        struct StatsConnection *connection = malloc(sizeof(struct StatsConnection));
        if (connection == NULL) {
            err("%s failed to allocate memory for new stats connection", __func__);
            return;
        }
        ev_io_init(&connection->watcher, connection_cb, sockfd, EV_READ | EV_WRITE);
        connection->watcher.data = connection;

        connection->input = new_buffer(256, loop);
        connection->output = new_buffer(16 * 1024, loop);

        ev_io_start(loop, &connection->watcher);
    }
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
            revents = 0; /* Clear revents so we don't try to send */
        } else if (bytes_received == 0) { /* peer closed socket */
            ev_io_stop(loop, &connection->watcher);
            close(w->fd);
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
