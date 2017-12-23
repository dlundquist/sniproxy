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
#include <sys/socket.h>
#include <netinet/in.h>
#include <ev.h>
#include "address.h"
#include "table.h"

SLIST_HEAD(Listener_head, Listener);

struct Listener {
    /* Configuration fields */
    struct Address *address, *fallback_address, *source_address;
    const struct Protocol *protocol;
    char *table_name;
    struct Logger *access_log;
    int log_bad_requests, reuseport, transparent_proxy, ipv6_v6only;

    /* Runtime fields */
    int reference_count;
    struct ev_io watcher;
    struct ev_timer backoff_timer;
    struct Table *table;
    SLIST_ENTRY(Listener) entries;
};


struct Listener *new_listener();
int accept_listener_arg(struct Listener *, char *);
int accept_listener_table_name(struct Listener *, char *);
int accept_listener_fallback_address(struct Listener *, char *);
int accept_listener_source_address(struct Listener *, char *);
int accept_listener_protocol(struct Listener *, char *);
int accept_listener_reuseport(struct Listener *, char *);
int accept_listener_ipv6_v6only(struct Listener *, char *);
int accept_listener_bad_request_action(struct Listener *, char *);

void add_listener(struct Listener_head *, struct Listener *);
void init_listeners(struct Listener_head *, const struct Table_head *, struct ev_loop *);
void listeners_reload(struct Listener_head *, struct Listener_head *, const struct Table_head *, struct ev_loop *);
void remove_listener(struct Listener_head *, struct Listener *, struct ev_loop *);
void free_listeners(struct Listener_head *, struct ev_loop *);

int valid_listener(const struct Listener *);
struct Address *listener_lookup_server_address(const struct Listener *,
        const char *, size_t);
void print_listener_config(FILE *, const struct Listener *);
void listener_ref_put(struct Listener *);
struct Listener *listener_ref_get(struct Listener *);

#endif
