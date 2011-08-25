#ifndef BACKEND_H
#define BACKEND_H

#include <netinet/in.h>
#include <sys/queue.h>
#include <pcre.h>

#define HOSTNAME_REGEX_LEN 256
#define BACKEND_ADDRESS_LEN 256

struct Backend {
	char hostname[HOSTNAME_REGEX_LEN];
	pcre *hostname_re;
	char address[BACKEND_ADDRESS_LEN];
	int port;

    STAILQ_ENTRY(Backend) entries;
};

void init_backends();
void free_backends();
void add_backend(const char *, const char *, int);
int lookup_backend_socket(const char *);

#endif
