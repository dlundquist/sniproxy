#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/queue.h>
#include <sys/select.h>
#include "connection.h"


#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))

/* Linux may not include _SAFE macros */
#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar)           \
    for ((var) = LIST_FIRST((head));                \
        (var) && ((tvar) = LIST_NEXT((var), field), 1);     \
        (var) = (tvar))
#endif


static LIST_HEAD(ConnectionHead, Connection) connections;


static void handle_connection_server_data(struct Connection *);
static void handle_connection_client_data(struct Connection *);
static void close_connection(struct Connection *);
static void free_connection(struct Connection *);


void
init_connections() {
    LIST_INIT(&connections);
}

void
free_connections() {
    struct Connection *iter;

    while ((iter = LIST_FIRST(&connections)) != NULL) {
        LIST_REMOVE(iter, entries);
        free_connection(iter);
    }
}

void
accept_connection(struct Listener *listener) {
    struct Connection *c;
    struct sockaddr_in client_addr;
    unsigned int client_addr_len;

    c = calloc(1, sizeof(struct Connection));
    if (c == NULL) {
        syslog(LOG_CRIT, "calloc failed");
        return;
    }

    client_addr_len = sizeof(client_addr);
    c->client.sockfd = accept(listener->sockfd, (struct sockaddr *) &client_addr, &client_addr_len);
    if (c->client.sockfd < 0) {
        syslog(LOG_NOTICE, "accept failed: %s", strerror(errno));
        free(c);
        return;
    }
    c->state = ACCEPTED;
    c->listener = listener;

    LIST_INSERT_HEAD(&connections, c, entries);
}

/*
 * Prepares the fd_set as a set of all active file descriptors in all our
 * currently active connections and one additional file descriptior fd that
 * can be used for a listening socket.
 * Returns the highest file descriptor in the set.
 */
int
fd_set_connections(fd_set *fds, int max) {
    struct Connection *iter;

    LIST_FOREACH(iter, &connections, entries) {
        switch(iter->state) {
            case(CONNECTED):
                if (iter->server.sockfd > FD_SETSIZE) {
                    syslog(LOG_WARNING, "File descriptor > than FD_SETSIZE, closing connection\n");
                    close_connection(iter);
                    break;
                }

                FD_SET(iter->server.sockfd, fds);
                max = MAX(max, iter->server.sockfd);
                /* Fall through */
            case(ACCEPTED):
                if (iter->client.sockfd > FD_SETSIZE) {
                    syslog(LOG_WARNING, "File descriptor > than FD_SETSIZE, closing connection\n");
                    close_connection(iter);
                    break;
                }

                FD_SET(iter->client.sockfd, fds);
                max = MAX(max, iter->client.sockfd);
                /* Fall through */
            case(CLOSED):
                /* do nothing */
                break;
            default:
                syslog(LOG_WARNING, "Invalid state %d", iter->state); 
        }
    }

    return max;
}

void
handle_connections(fd_set *rfds) {
    struct Connection *iter, *tmp;

    LIST_FOREACH_SAFE(iter, &connections, entries, tmp) {
        switch(iter->state) {
            case(CONNECTED):
                if (FD_ISSET (iter->server.sockfd, rfds))
                handle_connection_server_data(iter);
                /* Fall through */
            case(ACCEPTED):
                if (FD_ISSET (iter->client.sockfd, rfds))
                handle_connection_client_data(iter);
                break;
            case(CLOSED):
                LIST_REMOVE(iter, entries);
                free(iter);
                break;
            default:
                syslog(LOG_WARNING, "Invalid state %d", iter->state);
        }
    }
}

static void
handle_connection_server_data(struct Connection *con) {
    int n;

    n = read(con->server.sockfd, con->server.buffer, BUFFER_LEN);
    if (n < 0) {
        syslog(LOG_INFO, "read failed: %s", strerror(errno));
        return;
    } else if (n == 0) { /* Server closed socket */
        close_connection(con);
        return;
    }

    con->server.buffer_size = n;

    n = send(con->client.sockfd, con->server.buffer, con->server.buffer_size, MSG_DONTWAIT);
    if (n < 0) {
        syslog(LOG_INFO, "send failed: %s", strerror(errno));
        return;
    }
    /* TODO handle case where n < con->server.buffer_size */
    con->server.buffer_size = 0;
}

static void
handle_connection_client_data(struct Connection *con) {
    int n;
    const char *hostname;
    struct sockaddr_storage peeraddr;
    socklen_t peeraddr_len;
    char peeripstr[INET6_ADDRSTRLEN];
    int peerport;

    n = read(con->client.sockfd, con->client.buffer, BUFFER_LEN);
    if (n < 0) {
        syslog(LOG_INFO, "Read failed: %s", strerror(errno));
        return;
    } else if (n == 0) { /* Client closed socket */
        close_connection(con);
        return;
    }
    con->client.buffer_size = n;

    switch(con->state) {
        case(ACCEPTED):
            /* identify peer address */
            peeraddr_len = sizeof(peeraddr);
            getpeername(con->client.sockfd, (struct sockaddr*)&peeraddr, &peeraddr_len);

            if (peeraddr.ss_family == AF_INET) {
                struct sockaddr_in *s = (struct sockaddr_in *)&peeraddr;
                peerport = ntohs(s->sin_port);
                inet_ntop(AF_INET, &s->sin_addr, peeripstr, sizeof(peeripstr));
            } else { // AF_INET6
                struct sockaddr_in6 *s = (struct sockaddr_in6 *)&peeraddr;
                peerport = ntohs(s->sin6_port);
                inet_ntop(AF_INET6, &s->sin6_addr, peeripstr, sizeof(peeripstr));
            }

            hostname = con->listener->parse_packet(con->client.buffer, con->client.buffer_size);

            if (hostname == NULL) {
                syslog(LOG_INFO, "Request from %s:%d did not include a hostname", peeripstr, peerport);
                /*hexdump(con->client.buffer, con->client.buffer_size);*/
            } else {
                syslog(LOG_INFO, "Request for %s from %s:%d", hostname, peeripstr, peerport);
            }

            /* lookup server for hostname and connect */
            con->server.sockfd = lookup_server_socket(con->listener, hostname);
            if (con->server.sockfd < 0) {
                syslog(LOG_NOTICE, "Server connection failed to %s", hostname);
                close_connection(con);
                return;
            }
            con->state = CONNECTED;

            /* Fall through */
        case(CONNECTED):
            n = send(con->server.sockfd, con->client.buffer, con->client.buffer_size, MSG_DONTWAIT);
            if (n < 0) {
                syslog(LOG_INFO, "send failed: %s", strerror(errno));
                return;
            }
            /* TODO handle case where n < con->client.buffer_size */
            con->client.buffer_size = 0;
            break;
        case(CLOSED):	
            syslog(LOG_WARNING, "Received data from closed connection");
            break;
        default:
            syslog(LOG_WARNING, "Invalid state %d\n", con->state);
    }
}

static void
close_connection(struct Connection *c) {
    /* The server socket is not open yet, when before we are in the CONNECTED state */
    if (c->state == CONNECTED && close(c->server.sockfd) < 0)
        syslog(LOG_INFO, "close failed: %s", strerror(errno));

    if (close(c->client.sockfd) < 0)
        syslog(LOG_INFO, "close failed: %s", strerror(errno));

    c->state = CLOSED;
}

static void
free_connection(struct Connection *c) {
    close_connection(c);
    free(c);
}
