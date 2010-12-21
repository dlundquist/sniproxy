#ifndef BACKEND_H
#define BACKEND_H

#include <netinet/in.h>
#include <sys/queue.h>

#define BACKEND_HOSTNAME_LEN 256

struct Backend {
	char hostname[BACKEND_HOSTNAME_LEN];
	struct sockaddr_storage addr;

	LIST_ENTRY(Backend) entries;
};

void init_backends();
int lookup_backend_socket(const char *);

#endif
