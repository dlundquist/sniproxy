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
#include "connection.h"

/* Linux may not include _SAFE macros */
#ifndef TAILQ_FOREACH_SAFE
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                      \
        for ((var) = TAILQ_FIRST(head);                                 \
            (var) != TAILQ_END(head) &&                                 \
            ((tvar) = TAILQ_NEXT(var, field), 1);                       \
            (var) = (tvar))
#endif

#define IS_TEMPORARY_SOCKERR(_errno) (_errno == EAGAIN || _errno == EWOULDBLOCK || _errno == EINTR)


static TAILQ_HEAD(ConnectionHead, Connection) connections;


static void client_rx_cb(struct ev_loop *, struct ev_io *, int);
static void server_rx_cb(struct ev_loop *, struct ev_io *, int);
static void client_tx_cb(struct ev_loop *, struct ev_io *, int);
static void server_tx_cb(struct ev_loop *, struct ev_io *, int);
static void move_to_head_of_queue(struct Connection *);
static void handle_connection_client_hello(struct ev_loop *, struct Connection *);
static void close_connection(struct ev_loop *, struct Connection *);
static void close_client_connection(struct ev_loop *, struct Connection *);
static void close_server_connection(struct ev_loop *, struct Connection *);
static struct Connection *new_connection();
static void free_connection(struct Connection *);
static void print_connection(FILE *, const struct Connection *);
static void get_peer_address(const struct sockaddr_storage *, char *, size_t, int *);


void
init_connections() {
    TAILQ_INIT(&connections);
}

void
add_connection(struct ev_loop *loop, int sockfd, struct Listener *listener) {
    struct Connection *c;

    c = new_connection();
    if (c == NULL) {
        syslog(LOG_CRIT, "new_connection failed");
        close(sockfd);
        return;
    }

    ev_io_init(&c->client.rx_watcher, client_rx_cb, sockfd, EV_READ);
    ev_io_init(&c->client.tx_watcher, client_tx_cb, sockfd, EV_WRITE);
    c->client.rx_watcher.data = c;
    c->client.tx_watcher.data = c;
    c->state = ACCEPTED;
    c->listener = listener;

    TAILQ_INSERT_HEAD(&connections, c, entries);

    ev_io_start(loop, &c->client.rx_watcher);
}

void
free_connections() {
    struct Connection *iter;

    while ((iter = TAILQ_FIRST(&connections)) != NULL) {
        TAILQ_REMOVE(&connections, iter, entries);
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

static void
print_connection(FILE *file, const struct Connection *con) {
    char client[INET6_ADDRSTRLEN];
    char server[INET6_ADDRSTRLEN];
    int client_port = 0;
    int server_port = 0;

    switch (con->state) {
        case ACCEPTED:
            get_peer_address(&con->client.addr, client, sizeof(client), &client_port);
            fprintf(file, "ACCEPTED      %s %d %zu/%zu\t-\n",
                client, client_port, con->client.buffer->len, con->client.buffer->size);
            break;
        case CONNECTED:
            get_peer_address(&con->client.addr, client, sizeof(client), &client_port);
            get_peer_address(&con->server.addr, server, sizeof(server), &server_port);
            fprintf(file, "CONNECTED     %s %d %zu/%zu\t%s %d %zu/%zu\n",
                client, client_port, con->client.buffer->len, con->client.buffer->size,
                server, server_port, con->server.buffer->len, con->server.buffer->size);
            break;
        case SERVER_CLOSED:
            get_peer_address(&con->client.addr, client, sizeof(client), &client_port);
            fprintf(file, "SERVER_CLOSED %s %d %zu/%zu\t-\n",
                client, client_port, con->client.buffer->len, con->client.buffer->size);
            break;
        case CLIENT_CLOSED:
            get_peer_address(&con->server.addr, server, sizeof(server), &server_port);
            fprintf(file, "CLIENT_CLOSED -\t%s %d %zu/%zu\n",
                server, server_port, con->server.buffer->len, con->server.buffer->size);
            break;
        case CLOSED:
            fprintf(file, "CLOSED        -\t-\n");
            break;
        case NEW:
            fprintf(file, "NEW           -\t-\n");
    }
}


static void
client_rx_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct Connection *con = (struct Connection *)w->data;
    int n;

    n = buffer_recv(con->client.buffer, con->client.rx_watcher.fd, MSG_DONTWAIT);
    if (n < 0 && !IS_TEMPORARY_SOCKERR(errno)) {
        syslog(LOG_INFO, "recv failed: %s", strerror(errno));
        return;
    } else if (n == 0) { /* Client closed socket */
        close_client_connection(loop, con);
        return;
    }

    /* if we filled the client receive buffer, stop the watcher for a while */
    if (buffer_room(con->client.buffer) == 0)
        ev_io_stop(loop, w);

    switch (con->state) {
        case(ACCEPTED):
            handle_connection_client_hello(loop, con);
            break;
        case(CONNECTED):
            if (buffer_len(con->client.buffer))
                ev_io_start(loop, &con->server.tx_watcher);
            break;
        default:
            syslog(LOG_INFO, "Unexpected state %d in connection:%s()",
                    con->state, __func__);
    }
    move_to_head_of_queue(con);
}

static void
client_tx_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct Connection *con = (struct Connection *)w->data;
    int n;

    n = buffer_send(con->server.buffer, con->client.tx_watcher.fd, MSG_DONTWAIT);
    if (n < 0 && !IS_TEMPORARY_SOCKERR(errno)) {
        syslog(LOG_INFO, "send failed: %s", strerror(errno));
        return;
    }

    /* if we emptied the server receive buffer, stop the watcher for a while */
    if (buffer_len(con->server.buffer) == 0)
        ev_io_stop(loop, w);

    switch (con->state) {
        case(CONNECTED):
            if (buffer_room(con->server.buffer))
                ev_io_start(loop, &con->server.rx_watcher);
            break;
        case(ACCEPTED):
        case(SERVER_CLOSED):
            if (buffer_len(con->server.buffer) == 0)
                return close_connection(loop, con);
        default:
            syslog(LOG_INFO, "Unexpected state %d in connection:%s()",
                    con->state, __func__);
    }
    move_to_head_of_queue(con);
}

static void
server_rx_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct Connection *con = (struct Connection *)w->data;
    int n;

    n = buffer_recv(con->server.buffer, con->server.rx_watcher.fd, MSG_DONTWAIT);
    if (n < 0 && !IS_TEMPORARY_SOCKERR(errno)) {
        syslog(LOG_INFO, "recv failed: %s", strerror(errno));
        return;
    } else if (n == 0) { /* Server closed socket */
        close_server_connection(loop, con);
        return;
    }

    /* if we filled the server receive buffer, stop the watcher for a while */
    if (buffer_room(con->server.buffer) == 0)
        ev_io_stop(loop, w);

    switch (con->state) {
        case(CONNECTED):
            if (buffer_len(con->server.buffer))
                ev_io_start(loop, &con->client.tx_watcher);
            break;
        case(CLIENT_CLOSED):
            ev_io_stop(loop, w);
            break;
        default:
            syslog(LOG_INFO, "Unexpected state %d in connection:%s()",
                    con->state, __func__);
    }
    move_to_head_of_queue(con);
}

static void
server_tx_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct Connection *con = (struct Connection *)w->data;
    int n;

    n = buffer_send(con->client.buffer, con->server.rx_watcher.fd, MSG_DONTWAIT);
    if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
        syslog(LOG_INFO, "send failed: %s", strerror(errno));
        return;
    }

    /* if we emptied the client receive buffer, stop the watcher for a while */
    if (buffer_len(con->client.buffer) == 0)
        ev_io_stop(loop, w);

    switch (con->state) {
        case(CONNECTED):
            if (buffer_room(con->client.buffer))
                ev_io_start(loop, &con->client.rx_watcher);
            break;
        case(CLIENT_CLOSED):
            if (buffer_len(con->client.buffer) == 0)
                return close_connection(loop, con);
            break;
        default:
            syslog(LOG_INFO, "unspected state %d in connection:%s()",
                    con->state, __func__);
    }
    move_to_head_of_queue(con);
}

static void
move_to_head_of_queue(struct Connection *con) {
    TAILQ_REMOVE(&connections, con, entries);
    TAILQ_INSERT_HEAD(&connections, con, entries);
}

static void
handle_connection_client_hello(struct ev_loop *loop, struct Connection *con) {
    char buffer[1460]; /* TCP MSS over standard Ethernet and IPv4 */
    ssize_t len;
    char *hostname = NULL;
    int parse_result;
    char peeripstr[INET6_ADDRSTRLEN] = {'\0'};
    int peerport = 0;
    int sockfd;

    get_peer_address(&con->client.addr, peeripstr, sizeof(peeripstr), &peerport);

    len = buffer_peek(con->client.buffer, buffer, sizeof(buffer));

    parse_result = con->listener->parse_packet(buffer, len, &hostname);
    if (parse_result == -1) {
        return;  /* incomplete request: try again */
    } else if (parse_result == -2) {
        syslog(LOG_INFO, "Request from %s:%d did not include a hostname", peeripstr, peerport);
        return close_connection(loop, con);
    } else if (parse_result < -2) {
        syslog(LOG_INFO, "Unable to parse request from %s:%d", peeripstr, peerport);
        syslog(LOG_DEBUG, "parse() returned %d", parse_result);
        /* TODO optionally dump request to file */
        return close_connection(loop, con);
    }

    syslog(LOG_INFO, "Request for %s from %s:%d", hostname, peeripstr, peerport);

    /* lookup server for hostname and connect */
    sockfd = lookup_server_socket(con->listener, hostname);
    if (sockfd < 0) {
        syslog(LOG_NOTICE, "Server connection failed to %s", hostname);
        free(hostname);
        return close_connection(loop, con);
    }

    ev_io_init(&con->server.rx_watcher, server_rx_cb, sockfd, EV_READ);
    ev_io_init(&con->server.tx_watcher, server_tx_cb, sockfd, EV_WRITE);
    con->server.rx_watcher.data = con;
    con->server.tx_watcher.data = con;
    con->hostname = hostname;

    /* record server socket address,
     * passing this down from open_backend_socket() in lookup_server_socket()
     * would be cleaner
     */
    getpeername(con->server.rx_watcher.fd,
                (struct sockaddr *)&con->server.addr,
                &con->server.addr_len);

    con->state = CONNECTED;

    ev_io_start(loop, &con->server.rx_watcher);
    ev_io_start(loop, &con->server.tx_watcher);
}

static void
close_connection(struct ev_loop *loop, struct Connection *c) {
    if (c->state == CONNECTED || c->state == ACCEPTED || c->state == SERVER_CLOSED)
        close_client_connection(loop, c);

    if (c->state == CONNECTED || c->state == CLIENT_CLOSED)
        close_server_connection(loop, c);

    TAILQ_REMOVE(&connections, c, entries);
    free_connection(c);
}

/* Close client socket. Caller must ensure that it has not been closed before. */
static void
close_client_connection(struct ev_loop *loop, struct Connection *c) {
    ev_io_stop(loop, &c->client.rx_watcher);
    ev_io_stop(loop, &c->client.tx_watcher);

    if (close(c->client.rx_watcher.fd) < 0)
        syslog(LOG_INFO, "close failed: %s", strerror(errno));

    /* next state depends on previous state */
    if (c->state == CONNECTED)
        c->state = CLIENT_CLOSED;
    else
        c->state = CLOSED;
}

/* Close server socket. Caller must ensure that it has not been closed before. */
static void
close_server_connection(struct ev_loop *loop, struct Connection *c) {
    ev_io_stop(loop, &c->server.rx_watcher);
    ev_io_stop(loop, &c->server.tx_watcher);

    if (close(c->server.rx_watcher.fd) < 0)
        syslog(LOG_INFO, "close failed: %s", strerror(errno));

    /* next state depends on previous state */
    if (c->state == CLIENT_CLOSED)
        c->state = CLOSED;
    else
        c->state = SERVER_CLOSED;
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
free_connection(struct Connection *c) {
    if (c == NULL)
        return;

    free_buffer(c->client.buffer);
    free_buffer(c->server.buffer);
    free((void *)c->hostname);
    free(c);
}

static void
get_peer_address(const struct sockaddr_storage *addr, char *ip, size_t len, int *port) {
    switch (addr->ss_family) {
        case AF_INET:
            inet_ntop(AF_INET, &((struct sockaddr_in *)addr)->sin_addr, ip, len);
            if (port != NULL)
                *port = ntohs(((struct sockaddr_in *)addr)->sin_port);
            break;
        case AF_INET6:
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)addr)->sin6_addr, ip, len);
            if (port != NULL)
                *port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
            break;
    }
}

