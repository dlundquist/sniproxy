#ifndef CONNECTION_H
#define CONNECTION_H

#include <sys/queue.h>
#include <sys/select.h>
#include "listener.h"
#include "buffer.h"

#define BUFFER_LEN 4096

struct Connection {
    enum State {
        ACCEPTED,       /* Newly accepted client connection */
        CONNECTED,      /* Parsed client hello and connected to server */
        SERVER_CLOSED,  /* Client closed socket */
        CLIENT_CLOSED,  /* Server closed socket */
        CLOSED          /* Both sockets closed */
    } state;

    struct {
        int sockfd;
        struct Buffer *buffer;
    } client, server;
    struct Listener * listener;

    LIST_ENTRY(Connection) entries;
};

void init_connections();
void accept_connection(struct Listener *);
int fd_set_connections(fd_set *, fd_set *, int);
void handle_connections(fd_set *, fd_set *);
void free_connections();
void print_connections();

#endif
