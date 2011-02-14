#ifndef CONNECTION_H
#define CONNECTION_H

#include <sys/queue.h>
#include <sys/select.h>

#define BUFFER_LEN 4096

extern int connection_count;

struct Connection {
    enum State {
        ACCEPTED,
        CONNECTED,
        CLOSED
    } state;

    struct {
        int sockfd;
        char buffer[BUFFER_LEN];
        int buffer_size;
    } client, server;

    LIST_ENTRY(Connection) entries;
};

void init_connections();
void accept_connection(int);
int fd_set_connections(fd_set *, int);
void handle_connections(fd_set *);
void free_connections();

#endif
