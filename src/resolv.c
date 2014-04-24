/*
 * Copyright (c) 2014, Dustin Lundquist <dustin@null-ptr.net>
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
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ev.h>
#ifdef HAVE_LIBUDNS
#include <udns.h>
#endif
#include "resolv.h"
#include "address.h"
#include "logger.h"


#ifndef HAVE_LIBUDNS

int
resolv_init(struct ev_loop *loop) {
    return 0;
}

void
resolv_shutdown(struct ev_loop *loop) {
}

struct ResolvQuery *
resolv_query(const char *hostname, void (*client_cb)(struct Address *, void *), void (*client_free_cb)(void *), void *client_cb_data) {
    return NULL;
}

void
resolv_cancel(struct ResolvQuery *query_handle) {
}

#else

struct ResolvQuery {
    void (*client_cb)(struct Address *, void *);
    void (*client_free_cb)(void *);
    void *client_cb_data;
    struct dns_query *query;
};


static struct ev_io resolv_io_watcher;
static struct ev_timer resolv_timeout_watcher;


static void resolv_sock_cb(struct ev_loop *, struct ev_io *, int);
static void resolv_timeout_cb(struct ev_loop *, struct ev_timer *, int);
static void dns_query_cb(struct dns_ctx *, struct dns_rr_a4 *, void *);
static void dns_timer_setup_cb(struct dns_ctx *, int, void *);


int
resolv_init(struct ev_loop *loop) {
    struct dns_ctx *ctx = &dns_defctx;
    dns_init(ctx, 1);

    int sockfd = dns_sock(ctx);

    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    ev_io_init(&resolv_io_watcher, resolv_sock_cb, sockfd, EV_READ);
    resolv_io_watcher.data = ctx;

    ev_io_start(loop, &resolv_io_watcher);


    ev_timer_init(&resolv_timeout_watcher, resolv_timeout_cb, 0.0, 0.0);
    resolv_timeout_watcher.data = ctx;

    dns_set_tmcbck(ctx, dns_timer_setup_cb, loop);

    return sockfd;
}

void
resolv_shutdown(struct ev_loop * loop) {
    struct dns_ctx *ctx = (struct dns_ctx *)resolv_io_watcher.data;

    ev_io_stop(loop, &resolv_io_watcher);

    if (ev_is_active(&resolv_timeout_watcher))
        ev_timer_stop(loop, &resolv_timeout_watcher);

    dns_close(ctx);
}

struct ResolvQuery *
resolv_query(const char *hostname, void (*client_cb)(struct Address *, void *), void (*client_free_cb)(void *), void *client_cb_data) {
    struct dns_ctx *ctx = (struct dns_ctx *)resolv_io_watcher.data;

    /*
     * Wrap udns's call back in our own
     */
    struct ResolvQuery *cb_data = malloc(sizeof(struct ResolvQuery));
    if (cb_data == NULL) {
        err("Failed to allocate memory for DNS query callback data.");
        return NULL;
    }
    cb_data->client_cb = client_cb;
    cb_data->client_free_cb = client_free_cb;
    cb_data->client_cb_data = client_cb_data;

    /* Just resolving A records for now */
    cb_data->query = dns_submit_a4(ctx,
            hostname, 0,
            dns_query_cb, cb_data);
    if (cb_data->query == NULL) {
        err("Failed to submit DNS query: %s", dns_strerror(dns_status(ctx)));
        if (cb_data->client_free_cb != NULL)
            cb_data->client_free_cb(cb_data->client_cb_data);
        free(cb_data);
        return NULL;
    }

    return cb_data;
}

void
resolv_cancel(struct ResolvQuery *query_handle) {
    struct ResolvQuery *cb_data = (struct ResolvQuery *)query_handle;
    struct dns_ctx *ctx = (struct dns_ctx *)resolv_io_watcher.data;

    dns_cancel(ctx, cb_data->query);

    free(cb_data->query);
    if (cb_data->client_free_cb != NULL)
        cb_data->client_free_cb(cb_data->client_cb_data);
    free(cb_data);
}

/*
 * DNS UDP socket activity callback
 */
static void
resolv_sock_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct dns_ctx *ctx = (struct dns_ctx *)w->data;

    if (revents & EV_READ)
        dns_ioevent(ctx, ev_now(loop));
}

/*
 * Wrapper for client callback we provide to udns
 */
static void
dns_query_cb(struct dns_ctx *ctx, struct dns_rr_a4 *result, void *data) {
    struct ResolvQuery *cb_data = (struct ResolvQuery *)data;
    struct Address *address = NULL;

    if (result == NULL) {
        info("resolv: %s\n", dns_strerror(dns_status(ctx)));
    } else if (result->dnsa4_nrr > 0) {
        struct sockaddr_in sa = {
            .sin_family = AF_INET,
            .sin_port = 0,
            .sin_addr = result->dnsa4_addr[0],
        };

        address = new_address_sa((struct sockaddr *)&sa, sizeof(sa));
        if (address == NULL)
            err("Failed to allocate memory for DNS query result address");
    }

    cb_data->client_cb(address, cb_data->client_cb_data);

    if (cb_data->client_free_cb != NULL)
        cb_data->client_free_cb(cb_data->client_cb_data);
    free(cb_data);
    free(result);
    free(address);
}

/*
 * DNS timeout callback
 */
static void
resolv_timeout_cb(struct ev_loop *loop, struct ev_timer *w, int revents) {
    struct dns_ctx *ctx = (struct dns_ctx *)w->data;

    if (revents & EV_TIMER)
        dns_timeouts(ctx, 30, ev_now(loop));
}

/*
 * Callback to setup DNS timeout callback
 */
static void
dns_timer_setup_cb(struct dns_ctx *ctx, int timeout, void *data) {
    struct ev_loop *loop = (struct ev_loop *)data;

    if (ev_is_active(&resolv_timeout_watcher))
        ev_timer_stop(loop, &resolv_timeout_watcher);

    if (ctx != NULL && timeout >= 0) {
        ev_timer_set(&resolv_timeout_watcher, timeout, 0.0);
        ev_timer_start(loop, &resolv_timeout_watcher);
    }
}
#endif
