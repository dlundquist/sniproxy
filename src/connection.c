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
#include <arpa/inet.h>
#include <ev.h>
#include <assert.h>
#include "connection.h"


#define IS_TEMPORARY_SOCKERR(_errno) (_errno == EAGAIN || \
                                      _errno == EWOULDBLOCK || \
                                      _errno == EINTR)


static TAILQ_HEAD(ConnectionHead, Connection) connections;


static inline int client_socket_open(const struct Connection *);
static inline int server_socket_open(const struct Connection *);
static void client_cb(struct ev_loop *, struct ev_io *, int);
static void client_rx(struct Connection *, struct ev_loop *);
static void client_tx(struct Connection *, struct ev_loop *);
static void server_cb(struct ev_loop *, struct ev_io *, int);
static void server_rx(struct Connection *, struct ev_loop *);
static void server_tx(struct Connection *, struct ev_loop *);
static void handle_connection_client_hello(struct Connection *,
                                           struct ev_loop *);
static void close_connection(struct Connection *, struct ev_loop *);
static void close_client_socket(struct Connection *, struct ev_loop *);
static void close_server_socket(struct Connection *, struct ev_loop *);
static inline void move_to_head_of_queue(struct Connection *);
static struct Connection *new_connection();
static void free_connection(struct Connection *);
static void print_connection(FILE *, const struct Connection *);
static void get_peer_address(const struct sockaddr_storage *,
                             char *, size_t, int *);


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

    ev_io_init(&c->client.watcher, client_cb, sockfd, EV_READ);
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
client_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct Connection *con = (struct Connection *)w->data;
    int events = 0;

    assert(client_socket_open(con));

    if (revents & EV_READ)
        client_rx(con, loop);

    /* Client socket may have been closed by client_rx() */
    if (revents & EV_WRITE &&
            client_socket_open(con) &&
            buffer_len(con->server.buffer))
        client_tx(con, loop);

    if (con->state == ACCEPTED)
        handle_connection_client_hello(con, loop);

    switch (con->state) {
        case ACCEPTED:
        case CONNECTED:
            if (buffer_room(con->client.buffer))
                events |= EV_READ;

            if (buffer_len(con->server.buffer))
                events |= EV_WRITE;

            if (events == 0)
                ev_io_stop(loop, w);
            else if (events != revents) {
                ev_io_stop(loop, w);
                ev_io_set(w, w->fd, events);
                ev_io_start(loop, w);
            }

            break;
        case SERVER_CLOSED:
            if (buffer_len(con->server.buffer))
                events |= EV_WRITE;
            else
                close_client_socket(con, loop);

            if (events == 0)
                ev_io_stop(loop, w);
            else if (events != revents) {
                ev_io_stop(loop, w);
                ev_io_set(w, w->fd, events);
                ev_io_start(loop, w);
            }

            break;
        case CLOSED:
            TAILQ_REMOVE(&connections, con, entries);
            free_connection(con);
            return;
        default:
            /* no op */
            break;
    }

    move_to_head_of_queue(con);
}

static void
client_rx(struct Connection *con, struct ev_loop *loop) {
    int n;

    n = buffer_recv(con->client.buffer,
                    con->client.watcher.fd, MSG_DONTWAIT);
    if (n < 0) {
        if (IS_TEMPORARY_SOCKERR(errno))
            return;

        syslog(LOG_INFO, "recv failed: %s, closing connection",
                strerror(errno));

        close_client_socket(con, loop);
    } else if (n == 0) { /* Client closed socket */
        close_client_socket(con, loop);
    }

    if (con->state == CLIENT_CLOSED ||
            (con->state == CONNECTED && buffer_len(con->client.buffer))) {
        ev_io_stop(loop, &con->server.watcher);
        con->server.watcher.events |= EV_WRITE;
        ev_io_start(loop, &con->server.watcher);
    }
}

static void
client_tx(struct Connection *con, struct ev_loop *loop) {
    int n;

    n = buffer_send(con->server.buffer,
                    con->client.watcher.fd, MSG_DONTWAIT);
    if (n < 0) {
        if (IS_TEMPORARY_SOCKERR(errno))
            return;

        syslog(LOG_INFO, "send failed: %s, closing connection",
                strerror(errno));
        close_client_socket(con, loop);
    }

    if (server_socket_open(con) && buffer_room(con->server.buffer)) {
        ev_io_stop(loop, &con->server.watcher);
        con->server.watcher.events |= EV_READ;
        ev_io_start(loop, &con->server.watcher);
    }
}

static void
server_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct Connection *con = (struct Connection *)w->data;
    int events = 0;

    assert(server_socket_open(con));

    if (revents & EV_READ)
        server_rx(con, loop);

    /* Server socket may have been closed by server_rx() */
    if (revents & EV_WRITE &&
            server_socket_open(con) &&
            buffer_len(con->client.buffer))
        server_tx(con, loop);

    switch (con->state) {
        case CONNECTED:
            if (buffer_room(con->server.buffer))
                events |= EV_READ;

            if (buffer_len(con->client.buffer))
                events |= EV_WRITE;

            if (events == 0)
                ev_io_stop(loop, w);
            else if (events != revents) {
                ev_io_stop(loop, w);
                ev_io_set(w, w->fd, events);
                ev_io_start(loop, w);
            }

            break;
        case CLIENT_CLOSED:
            if (buffer_len(con->client.buffer))
                events |= EV_WRITE;
            else
                close_server_socket(con, loop);

            if (events == 0)
                ev_io_stop(loop, w);
            else if (events != revents) {
                ev_io_stop(loop, w);
                ev_io_set(w, w->fd, events);
                ev_io_start(loop, w);
            }

            break;
       case CLOSED:
            TAILQ_REMOVE(&connections, con, entries);
            free_connection(con);
            return;
       default:
            /* no op */
            break;
    }

    move_to_head_of_queue(con);
}

static void
server_rx(struct Connection *con, struct ev_loop *loop) {
    int n;

    n = buffer_recv(con->server.buffer,
                    con->server.watcher.fd, MSG_DONTWAIT);
    if (n < 0) {
        if (IS_TEMPORARY_SOCKERR(errno))
            return;

        syslog(LOG_INFO, "recv failed: %s, closing connection",
                         strerror(errno));
        close_server_socket(con, loop);
    } else if (n == 0) { /* Server closed socket */
        close_server_socket(con, loop);
    }

    if (con->state == SERVER_CLOSED ||
            (con->state == CONNECTED && buffer_len(con->server.buffer))) {
        ev_io_stop(loop, &con->client.watcher);
        con->client.watcher.events |= EV_WRITE;
        ev_io_start(loop, &con->client.watcher);
    }
}

static void
server_tx(struct Connection *con, struct ev_loop *loop) {
    int n;

    n = buffer_send(con->client.buffer,
                    con->server.watcher.fd, MSG_DONTWAIT);
    if (n < 0) {
        if (IS_TEMPORARY_SOCKERR(errno))
            return;

        syslog(LOG_INFO, "send failed: %s, closing connection",
                         strerror(errno));
        close_server_socket(con, loop);
    }

    if (client_socket_open(con) && buffer_room(con->client.buffer)) {
        ev_io_stop(loop, &con->client.watcher);
        con->client.watcher.events |= EV_READ;
        ev_io_start(loop, &con->client.watcher);
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
    char peeripstr[INET6_ADDRSTRLEN] = {'\0'};
    int peerport = 0;
    int sockfd;

    get_peer_address(&con->client.addr,
                     peeripstr, sizeof(peeripstr), &peerport);

    len = buffer_peek(con->client.buffer, buffer, sizeof(buffer));

    parse_result = con->listener->parse_packet(buffer, len, &hostname);
    if (parse_result == -1) {
        return;  /* incomplete request: try again */
    } else if (parse_result == -2) {
        syslog(LOG_INFO, "Request from %s:%d did not include a hostname",
               peeripstr, peerport);
        return close_client_socket(con, loop);
    } else if (parse_result < -2) {
        syslog(LOG_INFO, "Unable to parse request from %s:%d",
               peeripstr, peerport);
        syslog(LOG_DEBUG, "parse() returned %d", parse_result);
        /* TODO optionally dump request to file */
        return close_client_socket(con, loop);
    }
    con->hostname = hostname;

    /* lookup server for hostname and connect */
    sockfd = lookup_server_socket(con->listener, hostname);
    if (sockfd < 0) {
        syslog(LOG_NOTICE, "Server connection failed to %s", hostname);
        return close_client_socket(con, loop);
    }

    /* record server socket address,
     * passing this down from open_backend_socket() in lookup_server_socket()
     * would be cleaner
     */
    getpeername(sockfd,
                (struct sockaddr *)&con->server.addr,
                &con->server.addr_len);

    assert(con->state == ACCEPTED);
    con->state = CONNECTED;

    ev_io_init(&con->server.watcher, server_cb, sockfd, EV_READ | EV_WRITE);
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
    char client[INET6_ADDRSTRLEN];
    char server[INET6_ADDRSTRLEN];
    int client_port = 0;
    int server_port = 0;

    switch (con->state) {
        case NEW:
            fprintf(file, "NEW           -\t-\n");
            break;
        case ACCEPTED:
            get_peer_address(&con->client.addr,
                             client, sizeof(client), &client_port);
            fprintf(file, "ACCEPTED      %s %d %zu/%zu\t-\n",
                    client, client_port,
                    con->client.buffer->len, con->client.buffer->size);
            break;
        case CONNECTED:
            get_peer_address(&con->client.addr,
                             client, sizeof(client), &client_port);
            get_peer_address(&con->server.addr,
                             server, sizeof(server), &server_port);
            fprintf(file, "CONNECTED     %s %d %zu/%zu\t%s %d %zu/%zu\n",
                    client, client_port,
                    con->client.buffer->len, con->client.buffer->size,
                    server, server_port,
                    con->server.buffer->len, con->server.buffer->size);
            break;
        case SERVER_CLOSED:
            get_peer_address(&con->client.addr,
                             client, sizeof(client), &client_port);
            fprintf(file, "SERVER_CLOSED %s %d %zu/%zu\t-\n",
                    client, client_port,
                    con->client.buffer->len, con->client.buffer->size);
            break;
        case CLIENT_CLOSED:
            get_peer_address(&con->server.addr,
                             server, sizeof(server), &server_port);
            fprintf(file, "CLIENT_CLOSED -\t%s %d %zu/%zu\n",
                    server, server_port,
                    con->server.buffer->len, con->server.buffer->size);
            break;
        case CLOSED:
            fprintf(file, "CLOSED        -\t-\n");
            break;
    }
}

static void
get_peer_address(const struct sockaddr_storage *addr,
                 char *ip, size_t len, int *port) {
    switch (addr->ss_family) {
        case AF_INET:
            inet_ntop(AF_INET,
                      &((struct sockaddr_in *)addr)->sin_addr,
                      ip, len);
            if (port != NULL)
                *port = ntohs(((struct sockaddr_in *)addr)->sin_port);
            break;
        case AF_INET6:
            inet_ntop(AF_INET6,
                      &((struct sockaddr_in6 *)addr)->sin6_addr,
                      ip, len);
            if (port != NULL)
                *port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
            break;
    }
}

