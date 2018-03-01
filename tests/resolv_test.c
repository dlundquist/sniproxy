#include <stdio.h>
#include <ev.h>
#include <assert.h>
#include "resolv.h"
#include "address.h"

static int query_count = 0;


static void query_cb(struct Address *result, void *data) {
    int *query_count = (int *)data;
    char ip_buf[128];

    if (result != NULL &&
            address_is_sockaddr(result) &&
            display_address(result, ip_buf, sizeof(ip_buf))) {

        fprintf(stderr, "query resolved to %s\n", ip_buf);

        query_count++;
    }
}

static void
test_init_cb(struct ev_loop *loop __attribute__((unused)), struct ev_timer *w __attribute__((unused)), int revents) {
    if (revents & EV_TIMER)
        resolv_query("localhost", RESOLV_MODE_DEFAULT, query_cb, NULL, &query_count);
}

static void
timeout_cb(struct ev_loop *loop, struct ev_timer *w __attribute__((unused)), int revents) {
    if (revents & EV_TIMER)
        ev_break(loop, EVBREAK_ALL);
}


int main() {
    struct ev_loop *loop = EV_DEFAULT;
    struct ev_timer timeout_watcher;
    struct ev_timer init_watcher;

    resolv_init(loop, NULL, NULL, 0);

    ev_timer_init(&init_watcher, &test_init_cb, 0.0, 0.0);
    ev_timer_start(loop, &init_watcher);
    ev_timer_init(&timeout_watcher, &timeout_cb, 5.0, 0.0);
    ev_timer_start(loop, &timeout_watcher);

    ev_run(loop, 0);

    resolv_shutdown(loop);

    return 0;
}
