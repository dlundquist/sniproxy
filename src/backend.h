#ifndef BACKEND_H
#define BACKEND_H

#include <sys/queue.h>
#include <pcre.h>

#define HOSTNAME_REGEX_LEN 256
#define BACKEND_ADDRESS_LEN 256

struct Backend {
    char hostname[HOSTNAME_REGEX_LEN];
    char address[BACKEND_ADDRESS_LEN];
    int port;

    /* Runtime fields */
    pcre *hostname_re;
    STAILQ_ENTRY(Backend) entries;
};

int open_backend_socket(struct Backend *, const char *);

#endif
