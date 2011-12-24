#include <stdio.h>
#include <stdlib.h>
#include <strings.h> /* strncasecmp */
#include <ctype.h> /* tolower */
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <pcre.h>
#include <errno.h>
#include <syslog.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "backend.h"
#include "util.h"


static void free_backend(struct Backend *);


struct Backend *
add_backend(struct Backend_head *head, const char *hostname, const char *address, int port) {
    struct Backend *backend;
    const char *reerr;
    int i, len, reerroffset;

    backend = calloc(1, sizeof(struct Backend));
    if (backend == NULL) {
        syslog(LOG_CRIT, "calloc failed");
        return NULL;
    }

    len = strlen(hostname) + 1;
    backend->hostname = calloc(1, len);
    if (backend->hostname == NULL) {
        syslog(LOG_CRIT, "calloc failed");
        free_backend(backend);
        return NULL;
    }
    strncpy(backend->hostname, hostname, len);

    len = strlen(address) + 1;
    backend->address = calloc(1, len);
    if (backend->address == NULL) {
        syslog(LOG_CRIT, "calloc failed");
        free_backend(backend);
        return NULL;
    }
    /* Store address as lower case */
    for (i = 0; i < len && address[i] != '\0'; i++)
        backend->address[i] = tolower(address[i]);

    backend->port = port;

    backend->hostname_re = pcre_compile(hostname, 0, &reerr, &reerroffset, NULL);
    if (backend->hostname_re == NULL) {
        syslog(LOG_CRIT, "Regex compilation failed: %s, offset %d", reerr, reerroffset);
        free_backend(backend);
        return NULL;
    }

    syslog(LOG_DEBUG, "Parsed %s %s %d", hostname, address, port);
    STAILQ_INSERT_TAIL(head, backend, entries);

    return backend;
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
