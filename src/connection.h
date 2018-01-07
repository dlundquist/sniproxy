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
#ifndef CONNECTION_H
#define CONNECTION_H

#include <sys/socket.h>
#include <sys/queue.h>
#include <ev.h>
#include "listener.h"
#include "buffer.h"

struct Connection {
    enum State {
        NEW,            /* Before successful accept */
        ACCEPTED,       /* Newly accepted client connection */
        PARSED,         /* Parsed initial request and extracted hostname */
        RESOLVING,      /* DNS query in progress */
        RESOLVED,       /* Server socket address resolved */
        CONNECTED,      /* Connected to server */
        SERVER_CLOSED,  /* Client closed socket */
        CLIENT_CLOSED,  /* Server closed socket */
        CLOSED          /* Both sockets closed */
    } state;

    struct {
        struct sockaddr_storage addr;
        socklen_t addr_len;
        struct ev_io watcher;
        struct Buffer *buffer;
    } client, server;
    struct Listener *listener;
    const char *hostname; /* Requested hostname */
    size_t hostname_len;
    size_t header_len;
    struct ResolvQuery *query_handle;
    ev_tstamp established_timestamp;
    int use_proxy_header;

    TAILQ_ENTRY(Connection) entries;
};

void init_connections();
int accept_connection(struct Listener *, struct ev_loop *);
void free_connections(struct ev_loop *);
void print_connections();

#endif
