#include "connection.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "tls.h"
#include "util.h"
#include "backend.h"

static LIST_HEAD(ConnectionHead, Connection) connections;


static void handle_connection_server_data(struct Connection *);
static void handle_connection_client_data(struct Connection *);
static void close_connection(struct Connection *);


void
init_connections() {
	LIST_INIT(&connections);
}


void
accept_connection(int sockfd) {
	struct Connection *con;
	struct sockaddr_in client_addr;
	unsigned int client_addr_len;

	con = calloc(1, sizeof(struct Connection));
	if (con == NULL) {
		fprintf(stderr, "calloc failed\n");

		close_tls_socket(sockfd);
	} else {
		client_addr_len = sizeof(client_addr);

		con->client_sockfd = accept(sockfd, (struct sockaddr *) &client_addr, &client_addr_len);
		if (con->client_sockfd < 0) {
			perror("ERROR on accept");
			free(con);
			return;
		}
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
	int n;

	n = read(con->server_sockfd, con->server_buffer, BUFFER_LEN);
	if (n < 0) {
		perror("read()");
		return;
	} else if (n == 0) { /* Server closed socket */
		close_connection(con);
		return;
	}

	con->server_buffer_size = n;

	n = send(con->client_sockfd, con->server_buffer, con->server_buffer_size, MSG_DONTWAIT);
	if (n < 0) {
		perror("send()");
		return;
	} else {
		con->server_buffer_size = 0;
	}
}

static void
handle_connection_client_data(struct Connection *con) {
	int n;
	const char *hostname;

	n = read(con->client_sockfd, con->client_buffer, BUFFER_LEN);
	if (n < 0) {
		perror("read()");
		return;
	} else if (n == 0) { /* Client closed socket */
		close_connection(con);
		return;
	}
	con->client_buffer_size = n;



	switch(con->state) {
		case(ACCEPTED):
			hostname = parse_tls_header((uint8_t *)con->client_buffer, con->client_buffer_size);
			if (hostname == NULL) {
				close_connection(con);
				fprintf(stderr, "Request did not include a hostname\n");
				hexdump(con->client_buffer, con->client_buffer_size);
				return;
			}
			fprintf(stderr, "DEBUG: request for %s\n", hostname);

			/* lookup server for hostname and connect */
			con->server_sockfd = lookup_backend_socket(hostname);
			if (con->server_sockfd < 0) {
				fprintf(stderr, "DEBUG: server connection failed to %s\n", hostname);
				close_connection(con);
				return;
			}
			con->state = CONNECTED;

			/* Fall through */
		case(CONNECTED):
			n = send(con->server_sockfd, con->client_buffer, con->client_buffer_size, MSG_DONTWAIT);
			if (n < 0) {
				perror("send()");
				return;
			} else {
				con->client_buffer_size = 0;
			}
			break;
		case(CLOSED):
			fprintf(stderr, "Received data from closed connection\n");
			break;
		default:
			fprintf(stderr, "Invalid state %d\n", con->state);
	}
}

static void
close_connection(struct Connection *c) {
	if (c->state == CONNECTED)
		if (close(c->server_sockfd) < 0)
			perror("close()");

	if (close(c->client_sockfd) < 0)
		perror("close()");

	c->state = CLOSED;
}
