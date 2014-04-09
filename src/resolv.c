#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
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

void
resolv_query(const char *hostname, void (*client_cb)(struct Address *, void *), void *client_cb_data) {
}

#else

struct cb_data {
    void (*client_cb)(struct Address *, void *);
    void *client_cb_data;
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

void
resolv_query(const char *hostname, void (*client_cb)(struct Address *, void *), void *client_cb_data) {
    struct dns_ctx *ctx = (struct dns_ctx *)resolv_io_watcher.data;

    struct cb_data *cb_data = malloc(sizeof(struct cb_data));
    if (cb_data == NULL) {
        err("Failed to allocate memory for DNS query callback data.");
        return;
    }
    cb_data->client_cb = client_cb;
    cb_data->client_cb_data = client_cb_data;

    /* Just resolving A records for now */
    struct dns_query *q = dns_submit_a4(ctx,
            hostname, 0,
            dns_query_cb, cb_data);
    if (q == NULL) {
        err("Failed to submit DNS query: %s", dns_strerror(dns_status(ctx)));
        return;
    }
}

static void
resolv_sock_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct dns_ctx *ctx = (struct dns_ctx *)w->data;

    if (revents & EV_READ)
        dns_ioevent(ctx, ev_now(loop));
}

static void
dns_query_cb(struct dns_ctx *ctx, struct dns_rr_a4 *result, void *data) {
    struct cb_data *cb_data = (struct cb_data *)data;
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

    free(cb_data);
    free(address);
}

static void
resolv_timeout_cb(struct ev_loop *loop, struct ev_timer *w, int revents) {
    struct dns_ctx *ctx = (struct dns_ctx *)w->data;

    dns_timeouts(ctx, 30, ev_now(loop));
}

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
