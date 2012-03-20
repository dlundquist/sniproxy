#include <stdio.h>
#include <string.h>
#include <ctype.h> /* tolower */
#include <errno.h>
#include <syslog.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h> /* getaddrinfo */
#include <unistd.h> /* close */
#include <pcre.h>
#include "backend.h"
#include "util.h"


static void free_backend(struct Backend *);

struct Backend *
new_backend() {
    struct Backend *backend;

    backend = calloc(1, sizeof(struct Backend));
    if (backend == NULL) {
        perror("malloc");
        return NULL;
    }

    return backend;
}

int
accept_backend_arg(struct Backend *backend, char *arg) {
    char *ch;

    if (backend->hostname == NULL) {
        backend->hostname = strdup(arg);
        if (backend->hostname == NULL) {
            fprintf(stderr, "strdup failed");
            return -1;
        }
    } else if (backend->address == NULL) {
        backend->address = strdup(arg);
        if (backend->address == NULL) {
            fprintf(stderr, "strdup failed");
            return -1;
        }

        /* Store address as lower case */
        for (ch = backend->address; *ch == '\0'; ch++)
            *ch = tolower(*ch);
    } else if (backend->port == 0 && isnumeric(arg)) {
        backend->port = atoi(arg);
    } else {
        fprintf(stderr, "Unexpected table backend argument: %s\n", arg);
        return -1;
    }

    return 1;
}

void
add_backend(struct Backend_head *backends, struct Backend *backend) {
    STAILQ_INSERT_TAIL(backends, backend, entries);
}

int
init_backend(struct Backend *backend) {
    const char *reerr;
    int reerroffset;

    backend->hostname_re = pcre_compile(backend->hostname, 0, &reerr, &reerroffset, NULL);
    if (backend->hostname_re == NULL) {
        syslog(LOG_CRIT, "Regex compilation failed: %s, offset %d", reerr, reerroffset);
        return 0;
    }

    syslog(LOG_DEBUG, "Parsed %s %s %d", backend->hostname, backend->address, backend->port);

    return 1;
}

struct Backend *
lookup_backend(const struct Backend_head *head, const char *hostname) {
    struct Backend *iter;

    if (hostname == NULL)
        hostname = "";

    STAILQ_FOREACH(iter, head, entries) {
        if (pcre_exec(iter->hostname_re, NULL, hostname, strlen(hostname), 0, 0, NULL, 0) >= 0) {
            syslog(LOG_DEBUG, "%s matched %s", iter->hostname, hostname);
            return iter;
        } else {
            syslog(LOG_DEBUG, "%s didn't match %s", iter->hostname, hostname);
        }
    }
    return NULL;
}

void
remove_backend(struct Backend_head *head, struct Backend *backend) {
    STAILQ_REMOVE(head, backend, Backend, entries);
    free_backend(backend);
}

static void
free_backend(struct Backend *backend) {
    if (backend->hostname != NULL)
        free(backend->hostname);
    if (backend->address != NULL)
        free(backend->address);
    if (backend->hostname_re != NULL)
        pcre_free(backend->hostname_re);
    free(backend);
}

int
open_backend_socket(struct Backend *b, const char *req_hostname) {
    int sockfd = -1, error;
    struct addrinfo hints, *results, *iter;
    const char *cause = NULL;
    char portstr[6]; /* port numbers are < 65536 */

    const char *target_hostname = b->address;
    if (strcmp(target_hostname, "*") == 0)
        target_hostname = req_hostname;

    snprintf(portstr, 6, "%d", b->port);
    syslog(LOG_DEBUG, "Connecting to %s:%s", target_hostname, portstr);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    error = getaddrinfo(target_hostname, portstr, &hints, &results);
    if (error != 0) {
        syslog(LOG_NOTICE, "Lookup error: %s", gai_strerror(error));
        return -1;
    }

    for (iter = results; iter; iter = iter->ai_next) {
        sockfd = socket(iter->ai_family, iter->ai_socktype, iter->ai_protocol);
        if (sockfd < 0) {
            cause = "socket";
            continue;
        }

        if (connect(sockfd, iter->ai_addr, iter->ai_addrlen) < 0) {
            cause = "connect";
            close(sockfd);
            sockfd = -1;
            continue;
        }

        break;  /* okay we got one */
    }
    if (sockfd < 0)
        syslog(LOG_ERR, "%s error: %s", cause, strerror(errno));

    freeaddrinfo(results);
    return sockfd;
}
