/*
 * Copyright (c) 2011 and 2012, Dustin Lundquist <dustin@null-ptr.net>
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
#ifndef LISTENER_H
#define LISTENER_H
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "table.h"

SLIST_HEAD(Listener_head, Listener);

struct Listener {
    /* Configuration fields */
    struct sockaddr_storage addr;
    socklen_t addr_len;
    enum Protocol {
        TLS,
        HTTP
    } protocol;
    char *table_name;

    /* Runtime fields */
    int sockfd;
    int (*parse_packet)(const char*, size_t, char **);
    void (*close_client_socket)(int);
    struct Table *table;
    SLIST_ENTRY(Listener) entries;
};

struct Listener *new_listener();
int accept_listener_arg(struct Listener *, char *);
int accept_listener_table_name(struct Listener *, char *);
int accept_listener_protocol(struct Listener *, char *);

void add_listener(struct Listener_head *, struct Listener *);
int *init_listeners(struct Listener_head *, const struct Table_head *);
int fd_set_listeners(const struct Listener_head *, fd_set *, int);
void handle_listeners(const struct Listener_head *, const fd_set *, void (*)(const struct Listener *));
void remove_listener(struct Listener_head *, struct Listener *);
void free_listeners(struct Listener_head *);

int valid_listener(const struct Listener *);
void print_listener_config(FILE *, const struct Listener *);
void print_listener_status(FILE *, const struct Listener *);
int init_listener(struct Listener *, const struct Table_head *);
void free_listener(struct Listener *);

static inline int lookup_server_socket(const struct Listener *listener, const char *hostname) {
    return lookup_table_server_socket(listener->table, hostname);
}
#endif
