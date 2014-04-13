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
#include <signal.h>
#include <string.h> /* memset() */
#include <errno.h>
#include <ev.h>
#include "server.h"
#include "listener.h"
#include "connection.h"
#include "resolv.h"

static void signal_cb(struct ev_loop *, struct ev_signal *, int revents);

static struct Config *config;
static struct ev_signal sighup_watcher;
static struct ev_signal sigusr1_watcher;
static struct ev_signal sigint_watcher;
static struct ev_signal sigterm_watcher;

void
init_server(struct Config *c) {
    config = c;

    ev_signal_init(&sighup_watcher, signal_cb, SIGHUP);
    ev_signal_init(&sigusr1_watcher, signal_cb, SIGUSR1);
    ev_signal_init(&sigint_watcher, signal_cb, SIGINT);
    ev_signal_init(&sigterm_watcher, signal_cb, SIGTERM);
    ev_signal_start(EV_DEFAULT, &sighup_watcher);
    ev_signal_start(EV_DEFAULT, &sigusr1_watcher);
    ev_signal_start(EV_DEFAULT, &sigint_watcher);
    ev_signal_start(EV_DEFAULT, &sigterm_watcher);

    /* ignore SIGPIPE, or it will kill us */
    signal(SIGPIPE, SIG_IGN);

    init_listeners(&config->listeners, &config->tables, &config->alpn_tables);
}

void
run_server() {
    resolv_init(EV_DEFAULT);
    init_connections();

    ev_run(EV_DEFAULT, 0);

    free_connections(EV_DEFAULT);
    resolv_shutdown(EV_DEFAULT);
}

static void
signal_cb(struct ev_loop *loop, struct ev_signal *w, int revents) {
    if (revents & EV_SIGNAL) {
        switch (w->signum) {
            case SIGHUP:
                break;
            case SIGUSR1:
                print_connections();
                break;
            case SIGINT:
            case SIGTERM:
                ev_unloop(loop, EVUNLOOP_ALL);
        }
    }
}
