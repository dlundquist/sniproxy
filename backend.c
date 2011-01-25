#include <stdio.h>
#include <stdlib.h>
#include <strings.h> /* strncasecmp */
#include <ctype.h> /* tolower */
#include "backend.h"
#include "util.h"

static LIST_HEAD(, Backend) backends;


static struct Backend* lookup_backend(const char *);
static int open_backend_socket(struct Backend *);


void
init_backends() {
    LIST_INIT(&backends);
}

int
lookup_backend_socket(const char *hostname) {
    struct Backend *b;

    b = lookup_backend(hostname);
    if (b == NULL) {
        fprintf(stderr, "No match found for %s\n", hostname);
        return -1;
    }

    return open_backend_socket(b);
}

static struct Backend *
lookup_backend(const char *hostname) {
    struct Backend *iter;

    LIST_FOREACH(iter, &backends, entries) {
        if (strncasecmp(hostname, iter->hostname, BACKEND_HOSTNAME_LEN) == 0)
            return iter;
    }
    return NULL;
}

static int
open_backend_socket(struct Backend *b) {
    int sockfd;

    if (b == NULL)
        return -1;

    sockfd = socket(b->addr.ss_family, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket()");
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&b->addr, sizeof(b->addr)) < 0) {
        perror("connect()");
        return -1;
    }

    return sockfd;
}

void
add_backend(const char *hostname, const char *address, int port) {
    struct Backend *b;
    int i;

    b = lookup_backend(hostname);
    if (b != NULL) {
        fprintf(stderr, "backend for %s already exists, skipping\n", hostname);
        return;
    }

    b = calloc(1, sizeof(struct Backend));
    if (b == NULL) {
        fprintf(stderr, "calloc failed\n");
        return;
    }

    for (i = 0; i < BACKEND_HOSTNAME_LEN && hostname[i] != '\0'; i++)
        b->hostname[i] = tolower(hostname[i]);

    if (parse_address(&b->addr, address, port) == 0) {
        fprintf(stderr, "Unable to parse %s as an IP address\n", address);
        return;
    }

    fprintf(stderr, "Parsed %s %s %d\n", hostname, address, port);
    LIST_INSERT_HEAD(&backends, b, entries);
}
