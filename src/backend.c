/*
 * Copyright (c) 2011 and 2012, Dustin Lundquist <dustin@null-ptr.net>
 * Copyright (c) 2011 Manuel Kasper <mk@neon1.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h> /* tolower */
#include <syslog.h>
#include <sys/queue.h>
#include <pcre.h>
#include "backend.h"
#include "address.h"


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
    if (backend->hostname == NULL) {
        backend->hostname = strdup(arg);
        if (backend->hostname == NULL) {
            fprintf(stderr, "strdup failed");
            return -1;
        }
    } else if (backend->address == NULL) {
        /* Store address in lower case */
        for (char *c = arg; *c == '\0'; c++)
            *c = tolower(*c);

        backend->address = new_address(arg);
        if (backend->address == NULL) {
            fprintf(stderr, "invalid address: %s\n", arg);
            return -1;
        }
    } else if (address_port(backend->address) == 0 && is_numeric(arg)) {
        address_set_port(backend->address, atoi(arg));
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
    char address_buf[256];
    const char *reerr;
    int reerroffset;

    if (backend->hostname_re == NULL) {
        backend->hostname_re = pcre_compile(backend->hostname, 0, &reerr, &reerroffset, NULL);
        if (backend->hostname_re == NULL) {
            syslog(LOG_CRIT, "Regex compilation failed: %s, offset %d", reerr, reerroffset);
            return 0;
        }

        syslog(LOG_DEBUG, "Parsed %s %s",
                backend->hostname,
                display_address(backend->address, address_buf, sizeof(address_buf)));
    }

    return 1;
}

struct Backend *
lookup_backend(const struct Backend_head *head, const char *hostname) {
    struct Backend *iter;

    if (hostname == NULL)
        hostname = "";

    STAILQ_FOREACH(iter, head, entries)
        if (pcre_exec(iter->hostname_re, NULL, hostname, strlen(hostname), 0, 0, NULL, 0) >= 0)
            return iter;

    return NULL;
}

void
print_backend_config(FILE *file, const struct Backend *backend) {
    char address[256];

    fprintf(file, "\t%s %s\n",
            backend->hostname,
            display_address(backend->address, address, sizeof(address)));
}

void
remove_backend(struct Backend_head *head, struct Backend *backend) {
    STAILQ_REMOVE(head, backend, Backend, entries);
    free_backend(backend);
}

static void
free_backend(struct Backend *backend) {
    if (backend == NULL)
        return;

    free(backend->hostname);
    free(backend->address);
    if (backend->hostname_re != NULL)
        pcre_free(backend->hostname_re);
    free(backend);
}
