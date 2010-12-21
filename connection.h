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
	char client_buffer[BUFFER_LEN];
	int client_buffer_size;

	int server_sockfd;
	char server_buffer[BUFFER_LEN];
	int server_buffer_size;

	LIST_ENTRY(Connection) entries;
};

void init_connections();
void accept_connection(int sockfd);
int fd_set_connections(fd_set *, int);
void handle_connections(fd_set *);

#endif
