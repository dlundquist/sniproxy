#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <unistd.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "listener.h"
#include "connection.h"
#include "tls.h"
#include "http.h"

#define BACKLOG 5
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))


static void close_listener(struct Listener *);


static SLIST_HEAD(, Listener) listeners;

void
init_listeners() {
    SLIST_INIT(&listeners);
}

/*
 * Prepares the fd_set as a set of all active file descriptors in all our
 * currently active connections and one additional file descriptior fd that
 * can be used for a listening socket.
 * Returns the highest file descriptor in the set.
 */
int
fd_set_listeners(fd_set *fds, int max) {
    struct Listener *iter;

    SLIST_FOREACH(iter, &listeners, entries) {
        if (iter->sockfd > FD_SETSIZE) {
            syslog(LOG_WARNING, "File descriptor > than FD_SETSIZE\n");
            break;
        }

        FD_SET(iter->sockfd, fds);
        max = MAX(max, iter->sockfd);
    }

    return max;
}

void
handle_listeners(fd_set *rfds) {
    struct Listener *iter;

    SLIST_FOREACH(iter, &listeners, entries) {
        if (FD_ISSET (iter->sockfd, rfds))
            accept_connection(iter);
    }
}

struct Listener *
add_listener(const struct sockaddr * addr, size_t addr_len, int tls_flag, const char *table_name) {
    struct Listener *listener;

    listener = calloc(1, sizeof(struct Listener));
    if (listener == NULL) {
        syslog(LOG_CRIT, "calloc failed");
        return NULL;
    }

    if (addr_len > sizeof(listener->addr)) {
        syslog(LOG_CRIT, "addr too long");
        free_listener(listener);
        return NULL;
    }
    memcpy(&listener->addr, addr, addr_len);
    listener->addr_len = addr_len;
    listener->protocol = tls_flag ? TLS : HTTP;

    listener->table_name = strdup(table_name); 
    if (listener->table_name == NULL) {
        syslog(LOG_CRIT, "could not allocate table_name");
        free_listener(listener);
        return NULL;
    }

    // Runtime initialization below
    listener->table = lookup_table(listener->table_name);
    if (listener->table == NULL)
        listener->table = add_table(listener->table_name);
    
    listener->sockfd = socket(listener->addr.ss_family, SOCK_STREAM, 0);
    if (listener->sockfd < 0) {
        syslog(LOG_CRIT, "socket failed");
        free_listener(listener);
        return NULL;
    }

    // set SO_REUSEADDR on server socket to facilitate restart
    int reuseval = 1;
    setsockopt(listener->sockfd, SOL_SOCKET, SO_REUSEADDR, &reuseval, sizeof(reuseval));
    
    if (bind(listener->sockfd, (struct sockaddr *)&listener->addr, listener->addr_len) < 0) {
        syslog(LOG_CRIT, "bind failed");
        close(listener->sockfd);
        free_listener(listener);
        return NULL;
    }

    if (listen(listener->sockfd, BACKLOG) < 0) {
        syslog(LOG_CRIT, "listen failed");
        close(listener->sockfd);
        free_listener(listener);
        return NULL;
    }

    switch(listener->protocol) {
        case TLS:
            listener->parse_packet = parse_tls_header;
            listener->close_client_socket = close_tls_socket;
            break;
        case HTTP:
            listener->parse_packet = parse_http_header;
            listener->close_client_socket = close_http_socket;
            break;
        default:
            syslog(LOG_CRIT, "invalid protocol");
            free_listener(listener);
            return NULL;
    }

    SLIST_INSERT_HEAD(&listeners, listener, entries);
    
    return listener;
}

static void
close_listener(struct Listener * listener) {
    close(listener->sockfd);
}

void
free_listener(struct Listener *listener) {
    if (listener->table_name != NULL)
        free (listener->table_name);
    free (listener);
}

void
print_listener_config(struct Listener *listener) {
    char addr_str[INET_ADDRSTRLEN];
    union {
        struct sockaddr_storage *storage;
        struct sockaddr_in *sin;
        struct sockaddr_in6 *sin6;
        struct sockaddr_un *sun;
    } addr;
    
    addr.storage = &listener->addr;

    switch(addr.storage->ss_family) {
        case AF_UNIX:
            printf("listener unix:%s {\n", addr.sun->sun_path);
            break;
        case AF_INET:
            inet_ntop(AF_INET, &addr.sin->sin_addr, addr_str, listener->addr_len);
            printf("listener %s %d {\n", addr_str, ntohs(addr.sin->sin_port));
            break;
        case AF_INET6:
            inet_ntop(AF_INET6, &addr.sin6->sin6_addr, addr_str, listener->addr_len);
            printf("listener %s %d {\n", addr_str, ntohs(addr.sin6->sin6_port));
            break;
        default:
            break;
    }

    if (listener->protocol == TLS)
        printf("\tprotocol tls\n");
    else
        printf("\tprotocol http\n");

    if (listener->table_name)
        printf("\ttable %s\n", listener->table_name);

    printf("}\n\n");
}

void
free_listeners() {
    struct Listener *iter;

    while ((iter = SLIST_FIRST(&listeners)) != NULL) {
        SLIST_REMOVE_HEAD(&listeners, entries);
        close_listener(iter);
        free_listener(iter);
    }
}
