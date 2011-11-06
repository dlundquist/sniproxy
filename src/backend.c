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


#ifndef STAILQ_FOREACH_SAFE
#define STAILQ_FOREACH_SAFE(var, head, field, tvar)                     \
    for ((var) = STAILQ_FIRST((head));                          \
        (var) && ((tvar) = STAILQ_NEXT((var), field), 1);               \
        (var) = (tvar))
#endif

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

