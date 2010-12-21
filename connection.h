#ifndef CONNECTION_H
#define CONNECTION_H

#include <sys/queue.h>
#include <sys/select.h>

#define BUFFER_LEN 4096

enum State {
		ACCEPTED,
		CONNECTED,
		CLOSED
};

struct Connection {
	enum State state;
	int client_sockfd;
	int server_sockfd;
	char buffer[BUFFER_LEN];
	int buffer_size;
	LIST_ENTRY(Connection) entries;
};

void init_connections();
void accept_connection(int sockfd);
int fd_set_connections(fd_set *, int);
void handle_connections(fd_set *);

#endif
