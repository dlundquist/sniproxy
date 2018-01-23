/*
 * Copyright (c) 2011-2014, Dustin Lundquist <dustin@null-ptr.net>
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
#include <netinet/in.h>
#include <netdb.h> /* getaddrinfo */
#include <unistd.h> /* close */
#include <fcntl.h>
#include <arpa/inet.h>
#include <ev.h>
#include <assert.h>
#include "connection.h"
#include "resolv.h"
#include "address.h"
#include "protocol.h"
#include "logger.h"


#define IS_TEMPORARY_SOCKERR(_errno) (_errno == EAGAIN || \
                                      _errno == EWOULDBLOCK || \
                                      _errno == EINTR)


struct resolv_cb_data {
    struct Connection *connection;
    struct Address *address;
    struct ev_loop *loop;
};


static TAILQ_HEAD(ConnectionHead, Connection) connections;


static inline int client_socket_open(const struct Connection *);
static inline int server_socket_open(const struct Connection *);

static void reactivate_watcher(struct ev_loop *, struct ev_io *,
        const struct Buffer *, const struct Buffer *);

static void connection_cb(struct ev_loop *, struct ev_io *, int);
static void resolv_cb(struct Address *, void *);
static void reactivate_watchers(struct Connection *, struct ev_loop *);
static void parse_client_request(struct Connection *);
static void resolve_server_address(struct Connection *, struct ev_loop *);
static void initiate_server_connect(struct Connection *, struct ev_loop *);
static void close_connection(struct Connection *, struct ev_loop *);
static void close_client_socket(struct Connection *, struct ev_loop *);
static void abort_connection(struct Connection *);
static void close_server_socket(struct Connection *, struct ev_loop *);
static struct Connection *new_connection(struct ev_loop *);
static void log_connection(struct Connection *);
static void log_bad_request(struct Connection *, const char *, size_t, int);
static void free_connection(struct Connection *);
static void print_connection(FILE *, const struct Connection *);
static void free_resolv_cb_data(struct resolv_cb_data *);


void
init_connections() {
    TAILQ_INIT(&connections);
}

/**
 * Accept a new incoming connection
 *
 * Returns 1 on success or 0 on error;
 */
int
accept_connection(struct Listener *listener, struct ev_loop *loop) {
    struct Connection *con = new_connection(loop);
    if (con == NULL) {
        err("new_connection failed");
        return 0;
    }

#ifdef HAVE_ACCEPT4
    int sockfd = accept4(listener->watcher.fd,
                    (struct sockaddr *)&con->client.addr,
                    &con->client.addr_len,
                    SOCK_NONBLOCK);
#else
    int sockfd = accept(listener->watcher.fd,
                    (struct sockaddr *)&con->client.addr,
                    &con->client.addr_len);
#endif
    if (sockfd < 0) {
        int saved_errno = errno;

        warn("accept failed: %s", strerror(errno));
        free_connection(con);

        errno = saved_errno;
        return 0;
    }

#ifndef HAVE_ACCEPT4
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
#endif

    /* Avoiding type-punned pointer warning */
    struct ev_io *client_watcher = &con->client.watcher;
    ev_io_init(client_watcher, connection_cb, sockfd, EV_READ);
    con->client.watcher.data = con;
    con->state = ACCEPTED;
    con->listener = listener_ref_get(listener);
    con->established_timestamp = ev_now(loop);

    TAILQ_INSERT_HEAD(&connections, con, entries);

    ev_io_start(loop, client_watcher);

    return 1;
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
        con->state == PARSED ||
        con->state == RESOLVING ||
        con->state == RESOLVED ||
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
        ssize_t bytes_received = buffer_recv(input_buffer, w->fd, 0, loop);
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
        ssize_t bytes_transmitted = buffer_send(output_buffer, w->fd, 0, loop);
        if (bytes_transmitted < 0 && !IS_TEMPORARY_SOCKERR(errno)) {
            warn("send(): %s, closing connection",
                    strerror(errno));

            close_socket(con, loop);
        }
    }

    /* Handle any state specific logic */
    if (is_client && con->state == ACCEPTED)
        parse_client_request(con);
    if (is_client && con->state == PARSED)
        resolve_server_address(con, loop);
    if (is_client && con->state == RESOLVED)
        initiate_server_connect(con, loop);

    /* Close other socket if we have flushed corresponding buffer */
    if (con->state == SERVER_CLOSED && buffer_len(con->server.buffer) == 0)
        close_client_socket(con, loop);
    if (con->state == CLIENT_CLOSED && buffer_len(con->client.buffer) == 0)
        close_server_socket(con, loop);

    if (con->state == CLOSED) {
        TAILQ_REMOVE(&connections, con, entries);

        if (con->listener->access_log)
            log_connection(con);

        free_connection(con);
        return;
    }

    reactivate_watchers(con, loop);
}

static void
reactivate_watchers(struct Connection *con, struct ev_loop *loop) {
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

    /* At least one watcher is still active for this connection,
     * or DNS callback active */
    assert((ev_is_active(client_watcher) && con->client.watcher.events) ||
           (ev_is_active(server_watcher) && con->server.watcher.events) ||
           con->state == RESOLVING);

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

static void
parse_client_request(struct Connection *con) {
    const char *payload;
    size_t payload_len = buffer_coalesce(con->client.buffer, (const void **)&payload);
    char *hostname = NULL;

    int result = con->listener->protocol->parse_packet(payload, payload_len, &hostname);
    if (result < 0) {
        char client[INET6_ADDRSTRLEN + 8];

        if (result == -1) { /* incomplete request */
            if (buffer_room(con->client.buffer) > 0)
                return; /* give client a chance to send more data */

            warn("Request from %s exceeded %ld byte buffer size",
                    display_sockaddr(&con->client.addr, client, sizeof(client)),
                    buffer_size(con->client.buffer));
        } else if (result == -2) {
            warn("Request from %s did not include a hostname",
                    display_sockaddr(&con->client.addr, client, sizeof(client)));
        } else {
            warn("Unable to parse request from %s: parse_packet returned %d",
                    display_sockaddr(&con->client.addr, client, sizeof(client)),
                    result);

            if (con->listener->log_bad_requests)
                log_bad_request(con, payload, payload_len, result);
        }

        if (con->listener->fallback_address == NULL) {
            abort_connection(con);
            return;
        }
    }

    con->hostname = hostname;
    con->hostname_len = (size_t)result;
    con->state = PARSED;
}

static void
abort_connection(struct Connection *con) {
    assert(client_socket_open(con));

    buffer_push(con->server.buffer,
            con->listener->protocol->abort_message,
            con->listener->protocol->abort_message_len);

    con->state = SERVER_CLOSED;
}

static void
resolve_server_address(struct Connection *con, struct ev_loop *loop) {
    /* TODO avoid extra malloc in listener_lookup_server_address() */
    struct Address *server_address =
        listener_lookup_server_address(con->listener, con->hostname, con->hostname_len);

    if (server_address == NULL) {
        abort_connection(con);
        return;
    } else if (address_is_hostname(server_address)) {
#ifndef HAVE_LIBUDNS
        warn("DNS lookups not supported unless sniproxy compiled with libudns");
        free(server_address);
        abort_connection(con);
        return;
#else
        struct resolv_cb_data *cb_data = malloc(sizeof(struct resolv_cb_data));
        if (cb_data == NULL) {
            err("%s: malloc", __func__);
            free(server_address);
            abort_connection(con);
            return;
        }
        cb_data->connection = con;
        cb_data->address = server_address;
        cb_data->loop = loop;

        int resolv_mode = RESOLV_MODE_DEFAULT;
        if (con->listener->transparent_proxy) {
            char listener_address[ADDRESS_BUFFER_SIZE];
            switch (con->client.addr.ss_family) {
                case AF_INET:
                    resolv_mode = RESOLV_MODE_IPV4_ONLY;
                    break;
                case AF_INET6:
                    resolv_mode = RESOLV_MODE_IPV6_ONLY;
                    break;
                default:
                    warn("attempt to use transparent proxy with hostname %s "
                            "on non-IP listener %s, falling back to "
                            "non-transparent mode",
                            address_hostname(server_address),
                            display_sockaddr(con->listener->address,
                                    listener_address, sizeof(listener_address))
                            );
            }
        }

        con->query_handle = resolv_query(address_hostname(server_address),
                resolv_mode, resolv_cb,
                (void (*)(void *))free_resolv_cb_data, cb_data);

        con->state = RESOLVING;
#endif
    } else if (address_is_sockaddr(server_address)) {
        con->server.addr_len = address_sa_len(server_address);
        assert(con->server.addr_len <= sizeof(con->server.addr));
        memcpy(&con->server.addr, address_sa(server_address),
            con->server.addr_len);

        free(server_address);

        con->state = RESOLVED;
    } else {
        /* invalid address type */
        assert(0);
    }
}

static void
resolv_cb(struct Address *result, void *data) {
    struct resolv_cb_data *cb_data = (struct resolv_cb_data *)data;
    struct Connection *con = cb_data->connection;
    struct ev_loop *loop = cb_data->loop;

    if (con->state != RESOLVING) {
        info("resolv_cb() called for connection not in RESOLVING state");
        return;
    }

    if (result == NULL) {
        notice("unable to resolve %s, closing connection",
                address_hostname(cb_data->address));
        abort_connection(con);
    } else {
        assert(address_is_sockaddr(result));

        /* copy port from server_address */
        address_set_port(result, address_port(cb_data->address));

        con->server.addr_len = address_sa_len(result);
        assert(con->server.addr_len <= sizeof(con->server.addr));
        memcpy(&con->server.addr, address_sa(result), con->server.addr_len);

        con->state = RESOLVED;

        initiate_server_connect(con, loop);
    }

    con->query_handle = NULL;
    reactivate_watchers(con, loop);
}

static void
free_resolv_cb_data(struct resolv_cb_data *cb_data) {
    free(cb_data->address);
    free(cb_data);
}

static void
initiate_server_connect(struct Connection *con, struct ev_loop *loop) {
#ifdef HAVE_ACCEPT4
    int sockfd = socket(con->server.addr.ss_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
#else
    int sockfd = socket(con->server.addr.ss_family, SOCK_STREAM, 0);
#endif
    if (sockfd < 0) {
        char client[INET6_ADDRSTRLEN + 8];
        warn("socket failed: %s, closing connection from %s",
                strerror(errno),
                display_sockaddr(&con->client.addr, client, sizeof(client)));
        abort_connection(con);
        return;
    }

#ifndef HAVE_ACCEPT4
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
#endif

    if (con->listener->transparent_proxy &&
            con->client.addr.ss_family == con->server.addr.ss_family) {
        int on = 1;
#ifdef IP_TRANSPARENT
        int result = setsockopt(sockfd, SOL_IP, IP_TRANSPARENT, &on, sizeof(on));
#else
        int result = -EPERM;
        /* XXX error: not implemented would be better, but this shouldn't be
         * reached since it is prohibited in the configuration parser. */
#endif
        if (result < 0) {
            err("setsockopt IP_TRANSPARENT failed: %s", strerror(errno));
            close(sockfd);
            abort_connection(con);
            return;
        }

        result = bind(sockfd, (struct sockaddr *)&con->client.addr,
                con->client.addr_len);
        if (result < 0) {
            err("bind failed: %s", strerror(errno));
            close(sockfd);
            abort_connection(con);
            return;
        }
    } else if (con->listener->source_address) {
        int on = 1;
        int result = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if (result < 0) {
            err("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
            close(sockfd);
            abort_connection(con);
            return;
        }

        int tries = 5;
        do {
            result = bind(sockfd,
                    address_sa(con->listener->source_address),
                    address_sa_len(con->listener->source_address));
        } while (tries-- > 0
                && result < 0
                && errno == EADDRINUSE
                && address_port(con->listener->source_address) == 0);
        if (result < 0) {
            err("bind failed: %s", strerror(errno));
            close(sockfd);
            abort_connection(con);
            return;
        }
    }

    int result = connect(sockfd,
            (struct sockaddr *)&con->server.addr,
            con->server.addr_len);
    /* TODO retry connect in EADDRNOTAVAIL case */
    if (result < 0 && errno != EINPROGRESS) {
        close(sockfd);
        char server[INET6_ADDRSTRLEN + 8];
        warn("Failed to open connection to %s: %s",
                display_sockaddr(&con->server.addr, server, sizeof(server)),
                strerror(errno));
        abort_connection(con);
        return;
    }

    struct ev_io *server_watcher = &con->server.watcher;
    ev_io_init(server_watcher, connection_cb, sockfd, EV_WRITE);
    con->server.watcher.data = con;
    con->state = CONNECTED;

    ev_io_start(loop, server_watcher);
}

/* Close client socket.
 * Caller must ensure that it has not been closed before.
 */
static void
close_client_socket(struct Connection *con, struct ev_loop *loop) {
    assert(con->state != CLOSED
            && con->state != CLIENT_CLOSED);

    ev_io_stop(loop, &con->client.watcher);

    if (close(con->client.watcher.fd) < 0)
        warn("close failed: %s", strerror(errno));

    if (con->state == RESOLVING) {
        resolv_cancel(con->query_handle);
        con->state = PARSED;
    }

    /* next state depends on previous state */
    if (con->state == SERVER_CLOSED
            || con->state == ACCEPTED
            || con->state == PARSED
            || con->state == RESOLVING
            || con->state == RESOLVED)
        con->state = CLOSED;
    else
        con->state = CLIENT_CLOSED;
}

/* Close server socket.
 * Caller must ensure that it has not been closed before.
 */
static void
close_server_socket(struct Connection *con, struct ev_loop *loop) {
    assert(con->state != CLOSED
            && con->state != SERVER_CLOSED);

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

    if (con->state == CONNECTED
            || con->state == CLIENT_CLOSED)
        close_server_socket(con, loop);

    assert(con->state == ACCEPTED
            || con->state == PARSED
            || con->state == RESOLVING
            || con->state == RESOLVED
            || con->state == SERVER_CLOSED
            || con->state == CLOSED);

    if (con->state == ACCEPTED
            || con->state == PARSED
            || con->state == RESOLVING
            || con->state == RESOLVED
            || con->state == SERVER_CLOSED)
        close_client_socket(con, loop);

    assert(con->state == CLOSED);
}

/*
 * Allocate and initialize a new connection
 */
static struct Connection *
new_connection(struct ev_loop *loop) {
    struct Connection *con = calloc(1, sizeof(struct Connection));
    if (con == NULL)
        return NULL;

    con->state = NEW;
    con->client.addr_len = sizeof(con->client.addr);
    con->server.addr_len = sizeof(con->server.addr);
    con->hostname = NULL;
    con->hostname_len = 0;
    con->query_handle = NULL;

    con->client.buffer = new_buffer(4096, loop);
    if (con->client.buffer == NULL) {
        free_connection(con);
        return NULL;
    }

    con->server.buffer = new_buffer(4096, loop);
    if (con->server.buffer == NULL) {
        free_connection(con);
        return NULL;
    }

    return con;
}

static void
log_connection(struct Connection *con) {
    ev_tstamp duration;
    char client_address[ADDRESS_BUFFER_SIZE];
    char listener_address[ADDRESS_BUFFER_SIZE];
    char server_address[ADDRESS_BUFFER_SIZE];

    if (con->client.buffer->last_recv > con->server.buffer->last_recv)
        duration = con->client.buffer->last_recv - con->established_timestamp;
    else
        duration = con->server.buffer->last_recv - con->established_timestamp;

    display_sockaddr(&con->client.addr, client_address, sizeof(client_address));
    display_address(con->listener->address, listener_address, sizeof(listener_address));
    display_sockaddr(&con->server.addr, server_address, sizeof(server_address));

    log_msg(con->listener->access_log,
           LOG_NOTICE,
           "%s -> %s -> %s [%.*s] %ld/%ld bytes tx %ld/%ld bytes rx %1.3f seconds",
           client_address,
           listener_address,
           server_address,
           (int)con->hostname_len,
           con->hostname,
           con->server.buffer->tx_bytes,
           con->server.buffer->rx_bytes,
           con->client.buffer->tx_bytes,
           con->client.buffer->rx_bytes,
           duration);
}

static void
log_bad_request(struct Connection *con __attribute__((unused)), const char *req, size_t req_len, int parse_result) {
    size_t message_len = 64 + 6 * req_len;
    char *message = malloc(message_len);
    if (message == NULL) {
        err("log_bad_request: unable to allocate message buffer");
        return;
    }
    char *message_pos = message;
    char *message_end = message + message_len;

    message_pos += snprintf(message_pos, (size_t)(message_end - message_pos),
                            "parse_packet({");

    for (size_t i = 0; i < req_len; i++)
        message_pos += snprintf(message_pos, (size_t)(message_end - message_pos),
                                "0x%02hhx, ", (unsigned char)req[i]);

    message_pos -= 2;/* Delete the trailing ', ' */
    snprintf(message_pos, (size_t)(message_end - message_pos), "}, %ld, ...) = %d",
             req_len, parse_result);
    debug("%s", message);

    free(message);
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

    listener_ref_put(con->listener);
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
                    buffer_len(con->client.buffer), buffer_size(con->client.buffer));
            break;
        case PARSED:
            fprintf(file, "PARSED        %s %zu/%zu\t-\n",
                    display_sockaddr(&con->client.addr, client, sizeof(client)),
                    buffer_len(con->client.buffer), buffer_size(con->client.buffer));
            break;
        case RESOLVING:
            fprintf(file, "RESOLVING      %s %zu/%zu\t-\n",
                    display_sockaddr(&con->client.addr, client, sizeof(client)),
                    buffer_len(con->client.buffer), buffer_size(con->client.buffer));
            break;
        case RESOLVED:
            fprintf(file, "RESOLVED      %s %zu/%zu\t-\n",
                    display_sockaddr(&con->client.addr, client, sizeof(client)),
                    buffer_len(con->client.buffer), buffer_size(con->client.buffer));
            break;
        case CONNECTED:
            fprintf(file, "CONNECTED     %s %zu/%zu\t%s %zu/%zu\n",
                    display_sockaddr(&con->client.addr, client, sizeof(client)),
                    buffer_len(con->client.buffer), buffer_size(con->client.buffer),
                    display_sockaddr(&con->server.addr, server, sizeof(server)),
                    buffer_len(con->server.buffer), buffer_size(con->server.buffer));
            break;
        case SERVER_CLOSED:
            fprintf(file, "SERVER_CLOSED %s %zu/%zu\t-\n",
                    display_sockaddr(&con->client.addr, client, sizeof(client)),
                    buffer_len(con->client.buffer), buffer_size(con->client.buffer));
            break;
        case CLIENT_CLOSED:
            fprintf(file, "CLIENT_CLOSED -\t%s %zu/%zu\n",
                    display_sockaddr(&con->server.addr, server, sizeof(server)),
                    buffer_len(con->server.buffer), buffer_size(con->server.buffer));
            break;
        case CLOSED:
            fprintf(file, "CLOSED        -\t-\n");
            break;
    }
}
