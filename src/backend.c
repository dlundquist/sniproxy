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

static STAILQ_HEAD(, Backend) backends;


static struct Backend* lookup_backend(const char *);
static int open_backend_socket(struct Backend *, const char *);


void
init_backends() {
    STAILQ_INIT(&backends);
}

#ifndef STAILQ_FOREACH_SAFE
#define STAILQ_FOREACH_SAFE(var, head, field, tvar)                     \
    for ((var) = STAILQ_FIRST((head));                          \
        (var) && ((tvar) = STAILQ_NEXT((var), field), 1);               \
        (var) = (tvar))
#endif

void
free_backends() {
    struct Backend *iter, *temp;

    STAILQ_FOREACH_SAFE(iter, &backends, entries, temp) {
        STAILQ_REMOVE_HEAD(&backends, entries);
        free(iter);
    }
}

int
lookup_backend_socket(const char *hostname) {
    struct Backend *b;

    b = lookup_backend(hostname);
    if (b == NULL) {
        syslog(LOG_INFO, "No match found for %s", hostname);
        return -1;
    }

    return open_backend_socket(b, hostname);
}

static struct Backend *
lookup_backend(const char *hostname) {
    struct Backend *iter;

    if (hostname == NULL)
        hostname = "";

    STAILQ_FOREACH(iter, &backends, entries) {
        if (pcre_exec(iter->hostname_re, NULL, hostname, strlen(hostname), 0, 0, NULL, 0) >= 0) {
            syslog(LOG_DEBUG, "%s matched %s", iter->hostname, hostname);
            return iter;
        } else {
            syslog(LOG_DEBUG, "%s didn't match %s", iter->hostname, hostname);
        }
    }
    return NULL;
}

static int
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

void
add_backend(const char *hostname, const char *address, int port) {
    struct Backend *b;
    const char *reerr;
    int reerroffset;
    int i;

    b = calloc(1, sizeof(struct Backend));
    if (b == NULL) {
        syslog(LOG_CRIT, "calloc failed");
        return;
    }

    strncpy(b->hostname, hostname, HOSTNAME_REGEX_LEN - 1);

    b->hostname_re = pcre_compile(hostname, 0, &reerr, &reerroffset, NULL);
    if (b->hostname_re == NULL) {
        syslog(LOG_CRIT, "Regex compilation failed: %s, offset %d", reerr, reerroffset);
        free(b);
        return;
    }

    for (i = 0; i < BACKEND_ADDRESS_LEN && address[i] != '\0'; i++)
        b->address[i] = tolower(address[i]);

    b->port = port;

    syslog(LOG_DEBUG, "Parsed %s %s %d", hostname, address, port);
    STAILQ_INSERT_TAIL(&backends, b, entries);
}
