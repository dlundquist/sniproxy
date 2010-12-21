#include "connection.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include "tls.h"

static LIST_HEAD(ConnectionHead, Connection) connections;


static void handle_connection_server_data(struct Connection *);
static void handle_connection_client_data(struct Connection *);


void
init_connections() {
	LIST_INIT(&connections);
}


void
accept_connection(int sockfd) {
	struct Connection *con;

	con = calloc(1, sizeof(struct Connection));
	if (con == NULL) {
		fprintf(stderr, "calloc failed\n");

		close_tls_socket(sockfd);
	} else {
		con->client_sockfd = sockfd;
		con->state = ACCEPTED;
		LIST_INSERT_HEAD(&connections, con, entries);
	}
}

/*
 * Prepares the fd_set as a set of all active file descriptors in all our
 * currently active connections and one additional file descriptior fd that
 * can be used for a listening socket.
 * Returns the highest file descriptor in the set.
 */
int
fd_set_connections(fd_set *fds, int fd) {
	struct Connection *iter;
	int max = fd;

	FD_ZERO(fds);
	FD_SET(fd, fds);

	LIST_FOREACH(iter, &connections, entries) {
		if (iter->state == ACCEPTED || iter->state == CONNECTED) {
			FD_SET(iter->client_sockfd, fds);
			max = (max > iter->client_sockfd) ? max : iter->client_sockfd;
		}

		if (iter->state == CONNECTED) {
			FD_SET(iter->server_sockfd, fds);
			max = (max > iter->server_sockfd) ? max : iter->server_sockfd;
		}
	}

	return max;
}

void
handle_connections(fd_set *rfds) {
	struct Connection *iter;

	LIST_FOREACH(iter, &connections, entries) {
		switch(iter->state) {
			case(ACCEPTED):
				if (FD_ISSET (iter->client_sockfd, rfds))
					handle_connection_client_data(iter);
				break;
			case(CONNECTED):
				if (FD_ISSET (iter->server_sockfd, rfds))
					handle_connection_server_data(iter);

				if (FD_ISSET (iter->client_sockfd, rfds))
					handle_connection_client_data(iter);
				break;
			case(CLOSED):
				LIST_REMOVE(iter, entries);
				break;
			default:
				fprintf(stderr, "Invalid state %d\n", iter->state);
		}
	}
}

static void
handle_connection_server_data(struct Connection *con) {
}

static void
handle_connection_client_data(struct Connection *con) {
}
