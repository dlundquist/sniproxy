#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "backend.h"

static LIST_HEAD(, Backend) backends;


static void add_backend(const char *, const char *, int);
static int open_backend_socket(struct Backend *);


void
init_backends() {
	LIST_INIT(&backends);
	add_backend("localhost", "127.0.0.1", 443);
	add_backend("example.com", "10.0.0.7", 443);
}

int
lookup_backend_socket(const char *hostname) {
	struct Backend *iter;
	
	LIST_FOREACH(iter, &backends, entries) {
		if (strncasecmp(hostname, iter->hostname, BACKEND_HOSTNAME_LEN) == 0)
			return open_backend_socket(iter);
	}
	fprintf(stderr, "No match found for %s\n", hostname);
	return -1;
}

static int
open_backend_socket(struct Backend *b) {
	int sockfd;

	sockfd = socket(b->addr.ss_family, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("socket()");
		return (-1);
	}


	if (connect(sockfd, (struct sockaddr *)&b->addr, sizeof(b->addr)) < 0) {
		perror("connect()");
		return(-1);
	}

	return(sockfd);
}

static void
add_backend(const char *hostname, const char *address, int port) {
	struct Backend *be;
	int retval, i;

	be = calloc(1, sizeof(struct Backend));
	if (be == NULL) {
		fprintf(stderr, "calloc failed\n");
		return;
	}

	for (i = 0; i < BACKEND_HOSTNAME_LEN && hostname[i] != '\0'; i++)
		be->hostname[i] = toupper(hostname[i]);

	retval = inet_pton(AF_INET, address, &((struct sockaddr_in *)&be->addr)->sin_addr);
	if (retval == 1) {
		((struct sockaddr_in *)&be->addr)->sin_family = AF_INET;
		((struct sockaddr_in *)&be->addr)->sin_port = htons(port);
		fprintf(stderr, "Parsed %s %s %d as IPv4\n", hostname, address, port);
		LIST_INSERT_HEAD(&backends, be, entries);
		return;
	}

	retval = inet_pton(AF_INET6, address, &((struct sockaddr_in6 *)&be->addr)->sin6_addr);
	if (retval == 1) {
		((struct sockaddr_in6 *)&be->addr)->sin6_family = AF_INET6;
		((struct sockaddr_in6 *)&be->addr)->sin6_port = htons(port);
		fprintf(stderr, "Parsed %s %s %d as IPv6\n", hostname, address, port);
		LIST_INSERT_HEAD(&backends, be, entries);
		return;
	}

	fprintf(stderr, "Unable to parse %s as an IP address\n", address);
}
