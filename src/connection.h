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

#include <sys/queue.h>
#include <sys/select.h>
#include "listener.h"
#include "buffer.h"

#define BUFFER_LEN 4096

struct Connection {
    enum State {
        ACCEPTED,       /* Newly accepted client connection */
        CONNECTED,      /* Parsed client hello and connected to server */
        SERVER_CLOSED,  /* Client closed socket */
        CLIENT_CLOSED,  /* Server closed socket */
        CLOSED          /* Both sockets closed */
    } state;

    struct {
        int sockfd;
        struct Buffer *buffer;
    } client, server;
    struct Listener * listener;

    LIST_ENTRY(Connection) entries;
};

void init_connections();
void accept_connection(struct Listener *);
int fd_set_connections(fd_set *, fd_set *, int);
void handle_connections(fd_set *, fd_set *);
void free_connections();
void print_connections();

#endif
