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
#include <sys/queue.h>
#include <pcre.h>
#include <assert.h>
#include "backend.h"
#include "address.h"
#include "logger.h"


static void free_backend(struct Backend *);
static const char *backend_config_options(const struct Backend *);


struct Backend *
new_backend() {
    struct Backend *backend;

    backend = calloc(1, sizeof(struct Backend));
    if (backend == NULL) {
        err("malloc");
        return NULL;
    }

    return backend;
}

int
accept_backend_arg(struct Backend *backend, const char *arg) {
    if (backend->pattern == NULL) {
        backend->pattern = strdup(arg);
        if (backend->pattern == NULL) {
            err("strdup failed");
            return -1;
        }
    } else if (backend->address == NULL) {

        backend->address = new_address(arg);
        if (backend->address == NULL) {
            err("invalid address: %s", arg);
            return -1;
        }
#ifndef HAVE_LIBUDNS
        if (!address_is_sockaddr(backend->address)) {
            err("Only socket address backends are permitted when compiled without libudns");
            return -1;
        }
#endif
    } else if (address_port(backend->address) == 0 && is_numeric(arg)) {
        if (!address_set_port_str(backend->address, arg)) {
            err("Invalid port: %s", arg);
            return -1;
        }
    } else if (backend->use_proxy_header == 0 &&
        strcasecmp(arg, "proxy_protocol") == 0) {
        backend->use_proxy_header = 1;
    } else {
        err("Unexpected table backend argument: %s", arg);
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
    if (backend->pattern_re == NULL) {
        const char *reerr;
        int reerroffset;
        backend->pattern_re =
            pcre_compile(backend->pattern, 0, &reerr, &reerroffset, NULL);
        if (backend->pattern_re == NULL) {
            err("Regex compilation of \"%s\" failed: %s, offset %d",
                    backend->pattern, reerr, reerroffset);
            return 0;
        }

        char address[ADDRESS_BUFFER_SIZE];
        debug("Parsed %s %s",
                backend->pattern,
                display_address(backend->address,
                    address, sizeof(address)));
    }

    return 1;
}

struct BackendLookupResult
lookup_backend(const struct Backend_head *head, const char *name, size_t name_len) {
    struct BackendLookupResult result;
    struct Backend *iter;

    if (name == NULL) {
        name = "";
        name_len = 0;
    }

    STAILQ_FOREACH(iter, head, entries) {
        assert(iter->pattern_re != NULL);

        if ((result.matches[0] = pcre_exec(iter->pattern_re, NULL,
                    name, name_len, 0, 0, result.matches + 1, 31)) >= 0) {

            result.backend = iter;
            return result;
        }
    }

    result.backend = NULL;
    
    return result;
}

int apply_pattern(const char * name, const char * pattern, int * matches, char * dst, size_t result_len)
{
    int length = result_len > 1024 ? 1024 : result_len;
    const char * src = pattern;
    while(length > 0 && (*src)) {
        if (src[0] == '$' && src[1] != 0) {
            if (src[1] >= '0' && src[1] <= '9') {
                int stringnumber = src[1] - '0';
                int ret;
                src += 2;

                *dst=0;
                ret = pcre_copy_substring(name, matches + 1, matches[0], stringnumber, dst, length);
                if (ret < 0) {
                    return 0;
                }
                dst += ret;
                length -= ret;
            } else {
                *dst = src[1];
                dst++;
                length--;
                src += 2;
            }

        } else {
            *dst = *src;
            dst++;
            src++;
            length--;
        }
    }
    if (length) {
        *dst = 0;
        return 1;
    }
    return 0;
}

void
print_backend_config(FILE *file, const struct Backend *backend) {
    char address[ADDRESS_BUFFER_SIZE];

    fprintf(file, "\t%s %s%s\n",
            backend->pattern,
            display_address(backend->address, address, sizeof(address)),
            backend_config_options(backend));
}

static const char *
backend_config_options(const struct Backend *backend) {
    if (backend->use_proxy_header)
        return " proxy_protocol";
    else
        return "";
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

    free(backend->pattern);
    free(backend->address);
    if (backend->pattern_re != NULL)
        pcre_free(backend->pattern_re);
    free(backend);
}
