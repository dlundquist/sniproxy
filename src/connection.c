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
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h> /* getaddrinfo */
#include <unistd.h> /* close */
#include <arpa/inet.h>
#include <ev.h>
#include <assert.h>
#include "connection.h"
#include "address.h"
#include "protocol.h"
#include "logger.h"


#define IS_TEMPORARY_SOCKERR(_errno) (_errno == EAGAIN || \
                                      _errno == EWOULDBLOCK || \
                                      _errno == EINTR)


static TAILQ_HEAD(ConnectionHead, Connection) connections;


static inline int client_socket_open(const struct Connection *);
static inline int server_socket_open(const struct Connection *);

static void reactivate_watcher(struct ev_loop *, struct ev_io *,
        const struct Buffer *, const struct Buffer *);

static void connection_cb(struct ev_loop *, struct ev_io *, int);
static void handle_connection_client_hello(struct Connection *,
                                           struct ev_loop *);
static void close_connection(struct Connection *, struct ev_loop *);
static void close_client_socket(struct Connection *, struct ev_loop *);
static void close_server_socket(struct Connection *, struct ev_loop *);
static struct Connection *new_connection();
static void log_connection(struct Connection *);
static void free_connection(struct Connection *);
static void print_connection(FILE *, const struct Connection *);


void
init_connections() {
    TAILQ_INIT(&connections);
}

/*
 * Accept a new incoming connection
 */
void
accept_connection(const struct Listener *listener, struct ev_loop *loop) {
    struct Connection *con = new_connection();
    if (con == NULL) {
        err("new_connection failed");
        return;
    }

    int sockfd = accept(listener->watcher.fd,
                    (struct sockaddr *)&con->client.addr,
                    &con->client.addr_len);
    if (sockfd < 0) {
        warn("accept failed: %s", strerror(errno));
        free_connection(con);
        return;
    }

    /* Avoiding type-punned pointer warning */
    struct ev_io *client_watcher = &con->client.watcher;
    ev_io_init(client_watcher, connection_cb, sockfd, EV_READ);
    con->client.watcher.data = con;
    con->state = ACCEPTED;
    con->listener = listener;
    if (gettimeofday(&con->established_timestamp, NULL) < 0)
        err("gettimeofday() failed: %s", strerror(errno));

    TAILQ_INSERT_HEAD(&connections, con, entries);

    ev_io_start(loop, client_watcher);
}

/*
 * Close and free all connections
 */
void
free_connections(struct ev_loop *loop) {
    struct Connection *iter;
    while ((iter = TAILQ_FIRST(&connections)) != NULL) {
        TAILQ_REMOVE(&connections, iter, entries);
        close_connection(iter, loop);
        free_connection(iter);
    }
}

/* dumps a list of all connections for debugging */
void
print_connections() {
    char filename[] = "/tmp/sniproxy-connections-XXXXXX";

    int fd = mkstemp(filename);
    if (fd < 0) {
        warn("mkstemp failed: %s", strerror(errno));
        return;
    }

    FILE *temp = fdopen(fd, "w");
    if (temp == NULL) {
        warn("fdopen failed: %s", strerror(errno));
        return;
    }

    fprintf(temp, "Running connections:\n");
    struct Connection *iter;
    TAILQ_FOREACH(iter, &connections, entries)
        print_connection(temp, iter);

    if (fclose(temp) < 0)
        warn("fclose failed: %s", strerror(errno));

    notice("Dumped connections to %s", filename);
}

/*
 * Test is client socket is open
 *
 * Returns true iff the client socket is opened based on connection state.
 */
static inline int
client_socket_open(const struct Connection *con) {
    return con->state == ACCEPTED ||
        con->state == CONNECTED ||
        con->state == SERVER_CLOSED;
}

/*
 * Test is server socket is open
 *
 * Returns true iff the server socket is opened based on connection state.
 */
static inline int
server_socket_open(const struct Connection *con) {
    return con->state == CONNECTED ||
        con->state == CLIENT_CLOSED;
}

/*
 * Main client callback: this is used by both the client and server watchers
 *
 * The logic is almost the same except for:
 *  + input buffer
 *  + output buffer
 *  + how to close the socket
 *
 */
static void
connection_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct Connection *con = (struct Connection *)w->data;
    int is_client = &con->client.watcher == w;
    struct Buffer *input_buffer =
        is_client ? con->client.buffer : con->server.buffer;
    struct Buffer *output_buffer =
        is_client ? con->server.buffer : con->client.buffer;
    void (*close_socket)(struct Connection *, struct ev_loop *) =
        is_client ? close_client_socket : close_server_socket;

    /* Receive first in case the socket was closed */
    if (revents & EV_READ && buffer_room(input_buffer)) {
        ssize_t bytes_received = buffer_recv(input_buffer, w->fd, MSG_DONTWAIT);
        if (bytes_received < 0 && !IS_TEMPORARY_SOCKERR(errno)) {
            warn("recv(): %s, closing connection",
                    strerror(errno));

            close_socket(con, loop);
            revents = 0; /* Clear revents so we don't try to send */
        } else if (bytes_received == 0) { /* peer closed socket */
            close_socket(con, loop);
            revents = 0;
        }
    }

    /* Transmit */
    if (revents & EV_WRITE && buffer_len(output_buffer)) {
        ssize_t bytes_transmitted = buffer_send(output_buffer, w->fd, MSG_DONTWAIT);
        if (bytes_transmitted < 0 && !IS_TEMPORARY_SOCKERR(errno)) {
            warn("send(): %s, closing connection",
                    strerror(errno));

            close_socket(con, loop);
        }
    }

    /* Handle any state specific logic */
    if (is_client && con->state == ACCEPTED)
        handle_connection_client_hello(con, loop);

    /* Close other socket if we have flushed corresponding buffer */
    if (con->state == SERVER_CLOSED && buffer_len(con->server.buffer) == 0)
        close_client_socket(con, loop);
    if (con->state == CLIENT_CLOSED && buffer_len(con->client.buffer) == 0)
        close_server_socket(con, loop);

    if (con->state == CLOSED) {
        TAILQ_REMOVE(&connections, con, entries);
        log_connection(con);
        free_connection(con);
        return;
    }

    struct ev_io *client_watcher = &con->client.watcher;
    struct ev_io *server_watcher = &con->server.watcher;


    /* Reactivate watchers */
    if (client_socket_open(con))
        reactivate_watcher(loop, client_watcher,
                con->client.buffer, con->server.buffer);

    if (server_socket_open(con))
        reactivate_watcher(loop, server_watcher,
                con->server.buffer, con->client.buffer);

    /* Neither watcher is active when the corresponding socket is closed */
    assert(client_socket_open(con) || !ev_is_active(client_watcher));
    assert(server_socket_open(con) || !ev_is_active(server_watcher));

    /* At least one watcher is still active for this connection */
    assert((ev_is_active(client_watcher) && con->client.watcher.events) ||
           (ev_is_active(server_watcher) && con->server.watcher.events));

    ev_verify(loop);

    /* Move to head of queue, so we can find inactive connections */
    TAILQ_REMOVE(&connections, con, entries);
    TAILQ_INSERT_HEAD(&connections, con, entries);
}

static void
reactivate_watcher(struct ev_loop *loop, struct ev_io *w,
        const struct Buffer *input_buffer,
        const struct Buffer *output_buffer) {
    int events = 0;

    if (buffer_room(input_buffer))
        events |= EV_READ;

    if (buffer_len(output_buffer))
        events |= EV_WRITE;

    if (ev_is_active(w)) {
        if (events == 0)
            ev_io_stop(loop, w);
        else if (events != w->events) {
            ev_io_stop(loop, w);
            ev_io_set(w, w->fd, events);
            ev_io_start(loop, w);
        }
    } else if (events != 0) {
        ev_io_set(w, w->fd, events);
        ev_io_start(loop, w);
    }
}

/*
 * Handle client request: this combines several phases
 *  + parse the client request
 *  + lookup which server to use
 *  + resolve the server name if necessary
 *  + connect to the server
 *
 * These really need to be broken up into separate states
 */
static void
handle_connection_client_hello(struct Connection *con, struct ev_loop *loop) {
    char buffer[1460]; /* TCP MSS over standard Ethernet and IPv4 */
    ssize_t len;
    char *hostname = NULL;
    int parse_result;
    char peer_ip[INET6_ADDRSTRLEN + 8];
    int sockfd = -1;


    len = buffer_peek(con->client.buffer, buffer, sizeof(buffer));

    parse_result = con->listener->protocol->parse_packet(buffer, len, &hostname);
    if (parse_result == -1) {
        return;  /* incomplete request: try again */
    } else if (parse_result < -1) {
        if (parse_result == -2) {
            warn("Request from %s did not include a hostname",
                    display_sockaddr(&con->client.addr, peer_ip, sizeof(peer_ip)));
        } else {
            warn("Unable to parse request from %s",
                    display_sockaddr(&con->client.addr, peer_ip, sizeof(peer_ip)));
            debug("parse() returned %d", parse_result);
            /* TODO optionally dump request to file */
        }

        if (con->listener->fallback_address == NULL) {
            buffer_push(con->server.buffer,
                    con->listener->protocol->abort_message,
                    con->listener->protocol->abort_message_len);

            con->state = SERVER_CLOSED;
            return;
        }
    }
    con->hostname = hostname;
    /* TODO break the remainder out into other states */

    /* lookup server for hostname and connect */
    struct Address *server_address =
        listener_lookup_server_address(con->listener, hostname);
    if (server_address == NULL) {
        close_client_socket(con, loop);
        return;
    }

    if (address_is_hostname(server_address)) {
        int error;
        struct addrinfo hints, *results, *iter;
        char portstr[6];

        snprintf(portstr, sizeof(portstr), "%d", address_port(server_address));

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = PF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        /* TODO blocking DNS lookup */
        error = getaddrinfo(address_hostname(server_address), portstr,
                &hints, &results);
        if (error != 0) {
            warn("Lookup error: %s", gai_strerror(error));
            close_client_socket(con, loop);
            free(server_address);
            return;
        }
        free(server_address);

        for (iter = results; iter; iter = iter->ai_next) {
            sockfd = socket(iter->ai_family, iter->ai_socktype,
                    iter->ai_protocol);
            if (sockfd < 0)
                continue;

            /* TODO blocking connect */
            if (connect(sockfd, iter->ai_addr, iter->ai_addrlen) < 0) {
                close(sockfd);
                sockfd = -1;
                continue;
            }

            con->server.addr_len = iter->ai_addrlen;
            memcpy(&con->server.addr, iter->ai_addr, iter->ai_addrlen);

            break;
        }

        freeaddrinfo(results);
    } else if (address_is_sockaddr(server_address)) {
        con->server.addr_len = address_sa_len(server_address);
        memcpy(&con->server.addr, address_sa(server_address),
                con->server.addr_len);
        free(server_address);

        /* TODO blocking connect */
        sockfd = socket(con->server.addr.ss_family, SOCK_STREAM, 0);
        if (sockfd >= 0 &&
                connect(sockfd, (struct sockaddr *)&con->server.addr,
                    con->server.addr_len) < 0) {
            close(sockfd);
            sockfd = -1;
        }
    }

    if (sockfd < 0) {
        warn("Server connection failed to %s", hostname);
        close_client_socket(con, loop);
        return;
    }

    assert(con->state == ACCEPTED);
    con->state = CONNECTED;

    ev_io_init(&con->server.watcher,
            connection_cb, sockfd, EV_READ | EV_WRITE);
    con->server.watcher.data = con;
    ev_io_start(loop, &con->server.watcher);
}

/* Close client socket.
 * Caller must ensure that it has not been closed before.
 */
static void
close_client_socket(struct Connection *con, struct ev_loop *loop) {
    assert(con->state != CLOSED);
    assert(con->state != CLIENT_CLOSED);

    ev_io_stop(loop, &con->client.watcher);

    if (close(con->client.watcher.fd) < 0)
        warn("close failed: %s", strerror(errno));

    /* next state depends on previous state */
    if (con->state == SERVER_CLOSED || con->state == ACCEPTED)
        con->state = CLOSED;
    else
        con->state = CLIENT_CLOSED;
}

/* Close server socket.
 * Caller must ensure that it has not been closed before.
 */
static void
close_server_socket(struct Connection *con, struct ev_loop *loop) {
    assert(con->state != CLOSED);
    assert(con->state != SERVER_CLOSED);

    ev_io_stop(loop, &con->server.watcher);

    if (close(con->server.watcher.fd) < 0)
        warn("close failed: %s", strerror(errno));

    /* next state depends on previous state */
    if (con->state == CLIENT_CLOSED)
        con->state = CLOSED;
    else
        con->state = SERVER_CLOSED;
}

static void
close_connection(struct Connection *con, struct ev_loop *loop) {
    assert(con->state != NEW); /* only used during initialization */

    if (con->state == CONNECTED || con->state == CLIENT_CLOSED)
        close_server_socket(con, loop);

    /* State is now: ACCEPTED, SERVER_CLOSED or CLOSED */

    if (con->state == ACCEPTED || con->state == SERVER_CLOSED)
        close_client_socket(con, loop);

    assert(con->state == CLOSED);
}

/*
 * Allocate and initialize a new connection
 */
static struct Connection *
new_connection() {
    struct Connection *con = calloc(1, sizeof(struct Connection));
    if (con == NULL)
        return NULL;

    con->state = NEW;
    con->client.addr_len = sizeof(con->client.addr);
    con->server.addr_len = sizeof(con->server.addr);
    con->hostname = NULL;

    con->client.buffer = new_buffer(4096);
    if (con->client.buffer == NULL) {
        free_connection(con);
        return NULL;
    }

    con->server.buffer = new_buffer(4096);
    if (con->server.buffer == NULL) {
        free_connection(con);
        return NULL;
    }

    return con;
}

static void
log_connection(struct Connection *con) {
    struct timeval duration;
    char client_address[256];
    char listener_address[256];
    char server_address[256];

    if (timercmp(&con->client.buffer->last_recv, &con->server.buffer->last_recv, >))
        timersub(&con->client.buffer->last_recv, &con->established_timestamp, &duration);
    else
        timersub(&con->server.buffer->last_recv, &con->established_timestamp, &duration);

    display_sockaddr(&con->client.addr, client_address, sizeof(client_address));
    display_address(con->listener->address, listener_address, sizeof(listener_address));
    if (con->server.addr.ss_family == AF_UNSPEC)
        strcpy(server_address, "NONE");
    else
        display_sockaddr(&con->server.addr, server_address, sizeof(server_address));

    notice("%s -> %s -> %s [%s] %d/%d bytes rx %d/%d bytes tx %d.%06d seconds",
           client_address,
           listener_address,
           server_address,
           con->hostname,
           con->server.buffer->tx_bytes,
           con->server.buffer->rx_bytes,
           con->client.buffer->tx_bytes,
           con->client.buffer->rx_bytes,
           duration.tv_sec,
           duration.tv_usec);
}

/*
 * Free a connection and associated data
 *
 * Requires that no watchers remain active
 */
static void
free_connection(struct Connection *con) {
    if (con == NULL)
        return;

    free_buffer(con->client.buffer);
    free_buffer(con->server.buffer);
    free((void *)con->hostname); /* cast away const'ness */
    free(con);
}

static void
print_connection(FILE *file, const struct Connection *con) {
    char client[INET6_ADDRSTRLEN + 8];
    char server[INET6_ADDRSTRLEN + 8];

    switch (con->state) {
        case NEW:
            fprintf(file, "NEW           -\t-\n");
            break;
        case ACCEPTED:
            fprintf(file, "ACCEPTED      %s %zu/%zu\t-\n",
                    display_sockaddr(&con->client.addr, client, sizeof(client)),
                    con->client.buffer->len, con->client.buffer->size);
            break;
        case CONNECTED:
            fprintf(file, "CONNECTED     %s %zu/%zu\t%s %zu/%zu\n",
                    display_sockaddr(&con->client.addr, client, sizeof(client)),
                    con->client.buffer->len, con->client.buffer->size,
                    display_sockaddr(&con->server.addr, server, sizeof(server)),
                    con->server.buffer->len, con->server.buffer->size);
            break;
        case SERVER_CLOSED:
            fprintf(file, "SERVER_CLOSED %s %zu/%zu\t-\n",
                    display_sockaddr(&con->client.addr, client, sizeof(client)),
                    con->client.buffer->len, con->client.buffer->size);
            break;
        case CLIENT_CLOSED:
            fprintf(file, "CLIENT_CLOSED -\t%s %zu/%zu\n",
                    display_sockaddr(&con->server.addr, server, sizeof(server)),
                    con->server.buffer->len, con->server.buffer->size);
            break;
        case CLOSED:
            fprintf(file, "CLOSED        -\t-\n");
            break;
    }
}
