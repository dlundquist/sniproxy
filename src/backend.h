#ifndef BACKEND_H
#define BACKEND_H

#include <sys/queue.h>
#include <pcre.h>

STAILQ_HEAD(Backend_head, Backend);

struct Backend {
    char *hostname;
    char *address;
    int port;

    /* Runtime fields */
    pcre *hostname_re;
    STAILQ_ENTRY(Backend) entries;
};

void add_backend(struct Backend_head *, struct Backend *);
struct Backend *lookup_backend(const struct Backend_head *, const char *);
int open_backend_socket(struct Backend *, const char *);
void remove_backend(struct Backend_head *, struct Backend *);
struct Backend *new_backend();
int accept_backend_arg(struct Backend *, char *);


#endif
