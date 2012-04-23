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
#include <stdio.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include "server.h"
#include "connection.h"

static void sig_handler(int);

static volatile int running; /* For signal handler */
static volatile int sighup_received; /* For signal handler */
static volatile int sigusr1_received; /* For signal handler */
static struct Config *config;

int
init_server(struct Config * c) {
    config = c;
    
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGHUP, sig_handler);
    signal(SIGUSR1, sig_handler);
    /* ignore SIGPIPE, or it will kill us */
    signal(SIGPIPE, SIG_IGN);

    init_tables(&config->tables);

    return init_listeners(&config->listeners, &config->tables);
}

void
run_server() {
    int maxfd;
    fd_set rfds, wfds;


    init_connections();
    running = 1;


    while (running) {
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        maxfd = fd_set_listeners(&config->listeners, &rfds, 0);
        maxfd = fd_set_connections(&rfds, &wfds, maxfd);

        if (select(maxfd + 1, &rfds, &wfds, NULL, NULL) < 0) {
            /* select() might have failed because we received a signal, so we need to check */
            if (errno != EINTR) {
                perror("select");
                return;
            }
            /* We where inturrupted by a signal */
            if (sighup_received) {
                sighup_received = 0;
            }
            if (sigusr1_received) {
                sigusr1_received = 0;
                print_connections();
            }
            continue; /* our file descriptor sets are undefined, so select again */
        }

        handle_listeners(&config->listeners, &rfds, accept_connection);

        handle_connections(&rfds, &wfds);
    }

    free_listeners(&config->listeners);
    free_connections();
}

static void
sig_handler(int sig) {
    switch(sig) {
        case(SIGHUP):
            sighup_received = 1;
            break;
        case(SIGUSR1):
            sigusr1_received = 1;
            break;
        case(SIGINT):
        case(SIGTERM):
            running = 0;
    }
    signal(sig, sig_handler);
    /* Reinstall signal handler */
}
