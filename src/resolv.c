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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ev.h>
#include <errno.h>
#ifdef HAVE_LIBUDNS
#include <udns.h>
#endif
#include "resolv.h"
#include "address.h"
#include "logger.h"


#ifndef HAVE_LIBUDNS
/*
 * If we do not have a DNS resolution library stub out module as no ops
 */

int
resolv_init(struct ev_loop *loop, char **nameservers, char **search_domains,
        int mode) {
    (void)loop;
    (void)nameservers;
    (void)search_domains;
    (void)mode;

    return 0;
}

void
resolv_shutdown(struct ev_loop *loop) {
    (void)loop;
}

struct ResolvQuery *
resolv_query(const char *hostname, int mode,
        void (*client_cb)(struct Address *, void *),
        void (*client_free_cb)(void *), void *client_cb_data) {
    (void)hostname;
    (void)mode;
    (void)client_cb;
    (void)client_free_cb;
    (void)client_cb_data;

    return NULL;
}

void
resolv_cancel(struct ResolvQuery *query_handle) {
    (void)query_handle;
}

#else
/*
 * Implement DNS resolution interface using libudns
 */

struct ResolvQuery {
    void (*client_cb)(struct Address *, void *);
    void (*client_free_cb)(void *);
    void *client_cb_data;
    int resolv_mode;
    struct dns_query *queries[2];
    size_t response_count;
    struct Address **responses;
};


static int default_resolv_mode = 1 /* RESOLV_MODE_IPV4_ONLY */;
static struct ev_io resolv_io_watcher;
static struct ev_timer resolv_timeout_watcher;


static void resolv_sock_cb(struct ev_loop *, struct ev_io *, int);
static void resolv_timeout_cb(struct ev_loop *, struct ev_timer *, int);
static void dns_query_v4_cb(struct dns_ctx *, struct dns_rr_a4 *, void *);
static void dns_query_v6_cb(struct dns_ctx *, struct dns_rr_a6 *, void *);
static void dns_timer_setup_cb(struct dns_ctx *, int, void *);
static void process_client_callback(struct ResolvQuery *);
static inline int all_queries_are_null(struct ResolvQuery *);
static struct Address *choose_ipv4_first(struct ResolvQuery *);
static struct Address *choose_ipv6_first(struct ResolvQuery *);
static struct Address *choose_any(struct ResolvQuery *);


int
resolv_init(struct ev_loop *loop, char **nameservers, char **search, int mode) {
    struct dns_ctx *ctx = &dns_defctx;
    if (nameservers == NULL) {
        /* Nameservers not specified, use system resolver config */
        dns_init(ctx, 0);
    } else {
        dns_reset(ctx);

        for (int i = 0; nameservers[i] != NULL; i++)
            dns_add_serv(ctx, nameservers[i]);

        if (search != NULL)
            for (int i = 0; search[i] != NULL; i++)
                dns_add_srch(ctx, search[i]);
    }

    default_resolv_mode = mode;

    int sockfd = dns_open(ctx);
    if (sockfd < 0)
        fatal("Failed to open DNS resolver socket: %s",
                strerror(errno));

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
resolv_query(const char *hostname, int mode,
        void (*client_cb)(struct Address *, void *),
        void (*client_free_cb)(void *), void *client_cb_data) {
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
    cb_data->resolv_mode = mode != RESOLV_MODE_DEFAULT ?
                           mode : default_resolv_mode;
    memset(cb_data->queries, 0, sizeof(cb_data->queries));
    cb_data->response_count = 0;
    cb_data->responses = NULL;

    /* Submit A and AAAA queries */
    if (cb_data->resolv_mode != RESOLV_MODE_IPV6_ONLY) {
        cb_data->queries[0] = dns_submit_a4(ctx,
                hostname, 0,
                dns_query_v4_cb, cb_data);
        if (cb_data->queries[0] == NULL)
            err("Failed to submit DNS query: %s", dns_strerror(dns_status(ctx)));
    };

    if (cb_data->resolv_mode != RESOLV_MODE_IPV4_ONLY) {
        cb_data->queries[1] = dns_submit_a6(ctx,
                hostname, 0,
                dns_query_v6_cb, cb_data);
        if (cb_data->queries[1] == NULL)
            err("Failed to submit DNS query: %s", dns_strerror(dns_status(ctx)));
    }

    if (all_queries_are_null(cb_data)) {
        if (cb_data->client_free_cb != NULL)
            cb_data->client_free_cb(cb_data->client_cb_data);
        free(cb_data);
        cb_data = NULL;
    }

    return cb_data;
}

void
resolv_cancel(struct ResolvQuery *query_handle) {
    struct ResolvQuery *cb_data = (struct ResolvQuery *)query_handle;
    struct dns_ctx *ctx = (struct dns_ctx *)resolv_io_watcher.data;

    for (size_t i = 0; i < sizeof(cb_data->queries) / sizeof(cb_data->queries[0]); i++) {
        if (cb_data->queries[i] != NULL) {
            dns_cancel(ctx, cb_data->queries[i]);
            free(cb_data->queries[i]);
            cb_data->queries[i] = NULL;
        }
    }

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
dns_query_v4_cb(struct dns_ctx *ctx, struct dns_rr_a4 *result, void *data) {
    struct ResolvQuery *cb_data = (struct ResolvQuery *)data;

    if (result == NULL) {
        info("resolv: %s\n", dns_strerror(dns_status(ctx)));
    } else if (result->dnsa4_nrr > 0) {
        struct Address **new_responses = realloc(cb_data->responses,
                (cb_data->response_count + (size_t)result->dnsa4_nrr) *
                    sizeof(struct Address *));
        if (new_responses == NULL) {
            err("Failed to allocate memory for additional DNS responses");
        } else {
            cb_data->responses = new_responses;

            for (int i = 0; i < result->dnsa4_nrr; i++) {
                struct sockaddr_in sa = {
                    .sin_family = AF_INET,
                    .sin_port = 0,
                    .sin_addr = result->dnsa4_addr[i],
                };

                cb_data->responses[cb_data->response_count] =
                        new_address_sa((struct sockaddr *)&sa, sizeof(sa));
                if (cb_data->responses[cb_data->response_count] == NULL)
                    err("Failed to allocate memory for DNS query result address");
                else
                    cb_data->response_count++;
            }
        }
    }

    free(result);
    cb_data->queries[0] = NULL; /* mark A query as being completed */

    /* Once all queries have completed, call client callback */
    if (all_queries_are_null(cb_data))
        process_client_callback(cb_data);
}

static void
dns_query_v6_cb(struct dns_ctx *ctx, struct dns_rr_a6 *result, void *data) {
    struct ResolvQuery *cb_data = (struct ResolvQuery *)data;

    if (result == NULL) {
        info("resolv: %s\n", dns_strerror(dns_status(ctx)));
    } else if (result->dnsa6_nrr > 0) {
        struct Address **new_responses = realloc(cb_data->responses,
                (cb_data->response_count + (size_t)result->dnsa6_nrr) *
                    sizeof(struct Address *));
        if (new_responses == NULL) {
            err("Failed to allocate memory for additional DNS responses");
        } else {
            cb_data->responses = new_responses;

            for (int i = 0; i < result->dnsa6_nrr; i++) {
                struct sockaddr_in6 sa = {
                    .sin6_family = AF_INET6,
                    .sin6_port = 0,
                    .sin6_addr = result->dnsa6_addr[i],
                };

                cb_data->responses[cb_data->response_count] =
                        new_address_sa((struct sockaddr *)&sa, sizeof(sa));
                if (cb_data->responses[cb_data->response_count] == NULL)
                    err("Failed to allocate memory for DNS query result address");
                else
                    cb_data->response_count++;
            }
        }
    }

    free(result);
    cb_data->queries[1] = NULL; /* mark AAAA query as being completed */

    /* Once all queries have completed, call client callback */
    if (all_queries_are_null(cb_data))
        process_client_callback(cb_data);
}

/*
 * Called once all queries have been completed
 */
static void
process_client_callback(struct ResolvQuery *cb_data) {
    struct Address *best_address = NULL;

    if (cb_data->resolv_mode == RESOLV_MODE_IPV4_FIRST)
        best_address = choose_ipv4_first(cb_data);
    else if (cb_data->resolv_mode == RESOLV_MODE_IPV6_FIRST)
        best_address = choose_ipv6_first(cb_data);
    else
        best_address = choose_any(cb_data);

    cb_data->client_cb(best_address, cb_data->client_cb_data);

    for (size_t i = 0; i < cb_data->response_count; i++)
        free(cb_data->responses[i]);

    free(cb_data->responses);
    if (cb_data->client_free_cb != NULL)
        cb_data->client_free_cb(cb_data->client_cb_data);
    free(cb_data);
}

static struct Address *
choose_ipv4_first(struct ResolvQuery *cb_data) {
    for (size_t i = 0; i < cb_data->response_count; i++)
        if (address_is_sockaddr(cb_data->responses[i]) &&
                address_sa(cb_data->responses[i])->sa_family == AF_INET)
            return cb_data->responses[i];

    return choose_any(cb_data);;
}

static struct Address *
choose_ipv6_first(struct ResolvQuery *cb_data) {
    for (size_t i = 0; i < cb_data->response_count; i++)
        if (address_is_sockaddr(cb_data->responses[i]) &&
                address_sa(cb_data->responses[i])->sa_family == AF_INET6)
            return cb_data->responses[i];

    return choose_any(cb_data);;
}

static struct Address *
choose_any(struct ResolvQuery *cb_data) {
    if (cb_data->response_count >= 1)
        return cb_data->responses[0];

    return NULL;
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

static inline int
all_queries_are_null(struct ResolvQuery *cb_data) {
    int result = 1;

    for (size_t i = 0; i < sizeof(cb_data->queries) / sizeof(cb_data->queries[0]); i++)
        result = result && cb_data->queries[i] == NULL;

    return result;
}
#endif
