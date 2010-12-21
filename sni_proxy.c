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


#define HTTPS_PORT 4343 /* for development as non-root user */
#define BACKLOG 5

void server(int);
void handle (int);
void hexdump(const void *, int);

int
main() {
	int sockfd, clientsockfd;
	unsigned int clilen;
	struct sockaddr_in serv_addr, cli_addr;


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
	if (0) {
		clilen = sizeof(cli_addr);
		while(1) {
			clientsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
			if (clientsockfd < 0)  {
				perror("ERROR on accept");
				return (-1);
			}

			handle(clientsockfd);
		}
	}
	return (0);
}

/* TODO move this into a server module */
static volatile int running; /* For signal handler */

void
server(int sockfd) {
	int retval, maxfd;
	fd_set rfds;

	init_connections();

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

void
handle (int sockfd) {
	char buffer[BUFFER_LEN];
	int n;
	const char* hostname;

	bzero(buffer, BUFFER_LEN);
	n = read(sockfd, buffer, BUFFER_LEN);
	if (n < 0) {
		perror("ERROR reading from socket");
		return;
	}

	hostname = parse_tls_header((uint8_t *)buffer, n);
	if (hostname == NULL) {
		close_tls_socket(sockfd);

		fprintf(stderr, "Request did not include a hostname\n");
		hexdump(buffer, n);
		return;
	}


	fprintf(stderr, "Request for %s\n", hostname);
	close(sockfd);
}


void hexdump(const void *ptr, int buflen) {
	const unsigned char *buf = (const unsigned char*)ptr;
	int i, j;
	for (i=0; i<buflen; i+=16) {
		printf("%06x: ", i);
		for (j=0; j<16; j++) 
			if (i+j < buflen)
				printf("%02x ", buf[i+j]);
			else
				printf("   ");
		printf(" ");
		for (j=0; j<16; j++) 
			if (i+j < buflen)
				printf("%c", isprint(buf[i+j]) ? buf[i+j] : '.');
		printf("\n");
	}
}

