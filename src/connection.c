#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "connection.h"
#include "http.h"
#include "tls.h"
#include "util.h"


#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))

/* Linux may not include _SAFE macros */
#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar)           \
    for ((var) = LIST_FIRST((head));                \
        (var) && ((tvar) = LIST_NEXT((var), field), 1);     \
        (var) = (tvar))
#endif


static LIST_HEAD(ConnectionHead, Connection) connections;

static void handle_connection_hello(struct Connection *);
static void handle_connection_server_data(struct Connection *);
static void handle_connection_client_data(struct Connection *);
static void close_connection(struct Connection *);
static struct Connection *new_connection();
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

    c = new_connection();
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
fd_set_connections(fd_set *rfds, fd_set *wfds, int max) {
    struct Connection *iter;

    LIST_FOREACH(iter, &connections, entries) {
        switch(iter->state) {
            case(CONNECTED):
                if (iter->server.sockfd > FD_SETSIZE) {
                    syslog(LOG_WARNING, "File descriptor > than FD_SETSIZE, closing connection\n");
                    close_connection(iter);
                    break;
                }

                if (buffer_len(iter->client.buffer))
                    FD_SET(iter->server.sockfd, wfds);

                if (buffer_room(iter->server.buffer))
                    FD_SET(iter->server.sockfd, rfds);
                max = MAX(max, iter->server.sockfd);
                /* Fall through */
            case(ACCEPTED):
                if (iter->client.sockfd > FD_SETSIZE) {
                    syslog(LOG_WARNING, "File descriptor > than FD_SETSIZE, closing connection\n");
                    close_connection(iter);
                    break;
                }

                if (buffer_len(iter->server.buffer))
                    FD_SET(iter->client.sockfd, wfds);

                if (buffer_room(iter->client.buffer))
                    FD_SET(iter->client.sockfd, rfds);
                max = MAX(max, iter->client.sockfd);
                break;
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
handle_connections(fd_set *rfds, fd_set *wfds) {
    struct Connection *iter, *tmp;
    ssize_t len;

    LIST_FOREACH_SAFE(iter, &connections, entries, tmp) {
        switch(iter->state) {
            case(CONNECTED):
                if (FD_ISSET (iter->server.sockfd, rfds) && buffer_room(iter->server.buffer))
                    handle_connection_server_data(iter);

                if (FD_ISSET (iter->server.sockfd, wfds) && buffer_len(iter->client.buffer)) {
                    len = buffer_send(iter->client.buffer, iter->server.sockfd, MSG_DONTWAIT);
                    if (len < 0)
                        syslog(LOG_INFO, "send failed: %s", strerror(errno));
                }
                    
                /* Fall through */
            case(ACCEPTED):
                if (FD_ISSET (iter->client.sockfd, rfds) && buffer_room(iter->client.buffer))
                    handle_connection_client_data(iter);

                if (FD_ISSET (iter->client.sockfd, wfds) && buffer_len(iter->server.buffer)) {
                    len = buffer_send(iter->server.buffer, iter->client.sockfd, MSG_DONTWAIT);
                    if (len < 0)
                        syslog(LOG_INFO, "send failed: %s", strerror(errno));
                }

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

    n = buffer_recv(con->server.buffer, con->server.sockfd, MSG_DONTWAIT);
    if (n < 0) {
        syslog(LOG_INFO, "recv failed: %s", strerror(errno));
        return;
    } else if (n == 0) { /* Server closed socket */
        close_connection(con);
        return;
    }

    n = buffer_send(con->server.buffer, con->client.sockfd, MSG_DONTWAIT);
    if (n < 0) {
        syslog(LOG_INFO, "send failed: %s", strerror(errno));
        return;
    }
}

static void
handle_connection_client_data(struct Connection *con) {
    int n;

    n = buffer_recv(con->client.buffer, con->client.sockfd, MSG_DONTWAIT);
    if (n < 0) {
        syslog(LOG_INFO, "recv failed: %s", strerror(errno));
        return;
    } else if (n == 0) { /* Client closed socket */
        close_connection(con);
        return;
    }

    switch(con->state) {
        case(ACCEPTED):
            handle_connection_hello(con);
            /* Fall through */
        case(CONNECTED):
            n = buffer_send(con->client.buffer, con->server.sockfd, MSG_DONTWAIT);
            if (n < 0) {
                syslog(LOG_INFO, "send failed: %s", strerror(errno));
                return;
            }
            break;
        case(CLOSED):	
            syslog(LOG_WARNING, "Received data from closed connection");
            break;
        default:
            syslog(LOG_WARNING, "Invalid state %d\n", con->state);
    }
}

static void
handle_connection_hello(struct Connection *con) {
    char buffer[256];
    ssize_t len;
    const char *hostname;
    struct sockaddr_storage peeraddr;
    char peeripstr[INET6_ADDRSTRLEN];
    socklen_t peeraddr_len;
    int peerport = 0;

    len  = buffer_peek(con->client.buffer, buffer, sizeof(buffer));

    hostname = con->listener->parse_packet(buffer, len);


    /* identify peer address */
    peeraddr_len = sizeof(peeraddr);
    getpeername(con->client.sockfd, (struct sockaddr*)&peeraddr, &peeraddr_len);

    switch(peeraddr.ss_family) {
        case AF_INET:
            peerport = ntohs(((struct sockaddr_in *)&peeraddr)->sin_port);
            inet_ntop(AF_INET, &((struct sockaddr_in *)&peeraddr)->sin_addr, peeripstr, sizeof(peeripstr));
            break;
        case AF_INET6:
            peerport = ntohs(((struct sockaddr_in6 *)&peeraddr)->sin6_port);
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&peeraddr)->sin6_addr, peeripstr, sizeof(peeripstr));
            break;
    }


    if (hostname == NULL) {
        syslog(LOG_INFO, "Request from %s:%d did not include a hostname", peeripstr, peerport);
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

static struct Connection *
new_connection() {
    struct Connection *c;

    c = calloc(1, sizeof (struct Connection));
    if (c == NULL)
        return NULL;

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
    close_connection(c);

    if (c->client.buffer)
        free_buffer(c->client.buffer);

    if (c->server.buffer)
        free_buffer(c->server.buffer);

    free(c);
}
