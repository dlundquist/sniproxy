#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h> /* bzero() */
#include <unistd.h> /* close() */
#include <ctype.h> /* isprint() in hexdump */
#include <sys/select.h>
#include "connection.h"
#include "tls.h"
#include "util.h"
#include "backend.h"


#define HTTPS_PORT 4343 /* for development as non-root user */
#define BACKLOG 5

static void server(int);

int
main() {
	int sockfd;
	struct sockaddr_in serv_addr;


	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) {
		perror("ERROR opening socket");
		return -1;
	}

	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(HTTPS_PORT);

	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		perror("ERROR on binding");
		return (-1);
	}

	if (listen(sockfd, BACKLOG) < 0) {
		perror("ERROR listen();");
		return -1;
	}


	server(sockfd);

	return (0);
}

/* TODO move this into a server module */
static volatile int running; /* For signal handler */

static void
server(int sockfd) {
	int retval, maxfd;
	fd_set rfds;

	init_connections();
	init_backends();

	running = 1;

	while (running) {
		maxfd = fd_set_connections(&rfds, sockfd);

		retval = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if (retval < 0) {
			perror("select");
			return;
		}

		if (FD_ISSET (sockfd, &rfds))
			accept_connection(sockfd);
		
		handle_connections(&rfds);
	}
}
