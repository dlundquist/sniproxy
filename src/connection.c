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
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> /* getaddrinfo */
#include <unistd.h> /* close */
#include <arpa/inet.h>
#include <ev.h>
#include <assert.h>
#include "connection.h"
#include "address.h"


#define IS_TEMPORARY_SOCKERR(_errno) (_errno == EAGAIN || \
                                      _errno == EWOULDBLOCK || \
                                      _errno == EINTR)


static TAILQ_HEAD(ConnectionHead, Connection) connections;


static inline int client_socket_open(const struct Connection *);
static inline int server_socket_open(const struct Connection *);

static void rearm_watcher(struct ev_loop *, struct ev_io *,
        const struct Buffer *, const struct Buffer *);

static void connection_cb(struct ev_loop *, struct ev_io *, int);
static void handle_connection_client_hello(struct Connection *,
                                           struct ev_loop *);
static void close_connection(struct Connection *, struct ev_loop *);
static void close_client_socket(struct Connection *, struct ev_loop *);
static void close_server_socket(struct Connection *, struct ev_loop *);
static inline void move_to_head_of_queue(struct Connection *);
static struct Connection *new_connection();
static void free_connection(struct Connection *);
static void print_connection(FILE *, const struct Connection *);


void
init_connections() {
    TAILQ_INIT(&connections);
}

void
accept_connection(const struct Listener *listener, struct ev_loop *loop) {
    struct Connection *c;
    int sockfd;

    c = new_connection();
    if (c == NULL) {
        syslog(LOG_CRIT, "new_connection failed");
        return;
    }

    sockfd = accept(listener->rx_watcher.fd,
                    (struct sockaddr *)&c->client.addr,
                    &c->client.addr_len);
    if (sockfd < 0) {
        syslog(LOG_NOTICE, "accept failed: %s", strerror(errno));
        free_connection(c);
        return;
    }

    ev_io_init(&c->client.watcher, connection_cb, sockfd, EV_READ);
    c->client.watcher.data = c;
    c->state = ACCEPTED;
    c->listener = listener;

    TAILQ_INSERT_HEAD(&connections, c, entries);

    ev_io_start(loop, &c->client.watcher);
}

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
    struct Connection *iter;
    char filename[] = "/tmp/sniproxy-connections-XXXXXX";
    int fd;
    FILE *temp;

    fd = mkstemp(filename);
    if (fd < 0) {
        syslog(LOG_INFO, "mkstemp failed: %s", strerror(errno));
        return;
    }

    temp = fdopen(fd, "w");
    if (temp == NULL) {
        syslog(LOG_INFO, "fdopen failed: %s", strerror(errno));
        return;
    }

    fprintf(temp, "Running connections:\n");
    TAILQ_FOREACH(iter, &connections, entries) {
        print_connection(temp, iter);
    }

    if (fclose(temp) < 0)
        syslog(LOG_INFO, "fclose failed: %s", strerror(errno));

    syslog(LOG_INFO, "Dumped connections to %s", filename);
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

    ssize_t bytes_received;
    ssize_t bytes_transmitted;

    if (revents & EV_READ && buffer_room(input_buffer)) {
        bytes_received = buffer_recv(input_buffer, w->fd, MSG_DONTWAIT);
        if (bytes_received < 0 && !IS_TEMPORARY_SOCKERR(errno)) {
            syslog(LOG_INFO, "recv(): %s, closing connection",
                    strerror(errno));

            close_socket(con, loop);
            revents = 0; /* Clear revents so we don't try to send */
        } else if (bytes_received == 0) { /* peer closed socket */
            close_socket(con, loop);
            revents = 0;
        }
    }

    if (revents & EV_WRITE && buffer_len(output_buffer)) {
        bytes_transmitted = buffer_send(output_buffer, w->fd, MSG_DONTWAIT);
        if (bytes_transmitted < 0 && !IS_TEMPORARY_SOCKERR(errno)) {
            syslog(LOG_INFO, "send(): %s, closing connection",
                    strerror(errno));

            close_socket(con, loop);
        }
    }

    if (is_client && con->state == ACCEPTED)
        handle_connection_client_hello(con, loop);

    /* Close other socket if we have flushed corresponding buffer */
    if (con->state == SERVER_CLOSED && buffer_len(con->server.buffer) == 0)
        close_client_socket(con, loop);
    if (con->state == CLIENT_CLOSED && buffer_len(con->client.buffer) == 0)
        close_server_socket(con, loop);

    if (con->state == CLOSED) {
        TAILQ_REMOVE(&connections, con, entries);
        free_connection(con);
        return;
    }

    if (client_socket_open(con))
        rearm_watcher(loop, &con->client.watcher,
                con->client.buffer, con->server.buffer);

    if (server_socket_open(con))
        rearm_watcher(loop, &con->server.watcher,
                con->server.buffer, con->client.buffer);

    /* Neither watcher is active when the corresponding socket is closed */
    assert(client_socket_open(con) || !ev_is_active(&con->client.watcher));
    assert(server_socket_open(con) || !ev_is_active(&con->server.watcher));

    /* At least one watcher is still active for this connection */
    assert((ev_is_active(&con->client.watcher) && con->client.watcher.events) ||
           (ev_is_active(&con->server.watcher) && con->server.watcher.events));

    ev_verify(loop);

    move_to_head_of_queue(con);
}

static void
rearm_watcher(struct ev_loop *loop, struct ev_io *w,
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
move_to_head_of_queue(struct Connection *con) {
    TAILQ_REMOVE(&connections, con, entries);
    TAILQ_INSERT_HEAD(&connections, con, entries);
}

static void
handle_connection_client_hello(struct Connection *con, struct ev_loop *loop) {
    char buffer[1460]; /* TCP MSS over standard Ethernet and IPv4 */
    ssize_t len;
    char *hostname = NULL;
    int parse_result;
    char peer_ip[INET6_ADDRSTRLEN + 8];
    int sockfd = -1;

    len = buffer_peek(con->client.buffer, buffer, sizeof(buffer));

    parse_result = con->listener->parse_packet(buffer, len, &hostname);
    if (parse_result == -1) {
        return;  /* incomplete request: try again */
    } else if (parse_result == -2) {
        syslog(LOG_INFO, "Request from %s did not include a hostname",
                display_sockaddr(&con->client.addr, peer_ip, sizeof(peer_ip)));

        if (con->listener->fallback_address == NULL) {
            close_client_socket(con, loop);
            return;
        }
    } else if (parse_result < -2) {
        syslog(LOG_INFO, "Unable to parse request from %s",
                display_sockaddr(&con->client.addr, peer_ip, sizeof(peer_ip)));
        syslog(LOG_DEBUG, "parse() returned %d", parse_result);
        /* TODO optionally dump request to file */

        if (con->listener->fallback_address == NULL) {
            close_client_socket(con, loop);
            return;
        }
    }
    con->hostname = hostname;

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

        error = getaddrinfo(address_hostname(server_address), portstr,
                &hints, &results);
        if (error != 0) {
            syslog(LOG_NOTICE, "Lookup error: %s", gai_strerror(error));
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

        sockfd = socket(con->server.addr.ss_family, SOCK_STREAM, 0);
        if (sockfd >= 0 &&
                connect(sockfd, (struct sockaddr *)&con->server.addr,
                    con->server.addr_len) < 0) {
            close(sockfd);
            sockfd = -1;
        }
    }

    if (sockfd < 0) {
        syslog(LOG_NOTICE, "Server connection failed to %s", hostname);
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
        syslog(LOG_INFO, "close failed: %s", strerror(errno));

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
        syslog(LOG_INFO, "close failed: %s", strerror(errno));

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

    /* state will now be either: ACCEPTED, SERVER_CLOSED or CLOSED */

    if (con->state == ACCEPTED || con->state == SERVER_CLOSED)
        close_client_socket(con, loop);

    assert(con->state == CLOSED);
}

static struct Connection *
new_connection() {
    struct Connection *c;

    c = calloc(1, sizeof(struct Connection));
    if (c == NULL)
        return NULL;

    c->state = NEW;
    c->client.addr_len = sizeof(c->client.addr);
    c->server.addr_len = sizeof(c->server.addr);
    c->hostname = NULL;

    c->client.buffer = new_buffer();
    if (c->client.buffer == NULL) {
        free_connection(c);
        return NULL;
    }

    c->server.buffer = new_buffer();
    if (c->server.buffer == NULL) {
        free_connection(c);
        return NULL;
    }

    return c;
}

static void
free_connection(struct Connection *con) {
    if (con == NULL)
        return;

    free_buffer(con->client.buffer);
    free_buffer(con->server.buffer);
    free((char *)con->hostname); /* cast away const'ness */
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
