#include <stdio.h>
#include <unistd.h> /* close() */
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include "server.h"
#include "listener.h"
#include "connection.h"
#include "config.h"
#include "util.h"

static void sig_handler(int);

static volatile int running; /* For signal handler */
static volatile int sighup_received; /* For signal handler */


int
init_server(const char *address, int port, int tls_flag) {
    struct sockaddr_storage addr;
    int addr_len;
    struct Listener *listener;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGHUP, sig_handler);
    /* ignore SIGPIPE, or it will kill us */
    signal(SIGPIPE, SIG_IGN);

    init_listeners();


    addr_len = parse_address(&addr, address, port);
    if (addr_len == 0) {
        perror("Error parsing address");
        return -1;
    }

    listener = add_listener((const struct sockaddr *)&addr, addr_len, tls_flag, "default");

    return listener->sockfd;
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
        maxfd = fd_set_listeners(&rfds, 0);
        maxfd = fd_set_connections(&rfds, &wfds, maxfd);

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
            /* select() might have failed because we received a signal, so we need to check */
            if (errno != EINTR) {
                perror("select");
                return;
            }
            /* We where inturrupted by a signal */
            if (sighup_received) {
                sighup_received = 0;
                load_config();
            }
            continue; /* our file descriptor sets are undefined, so select again */
        }

        handle_listeners(&rfds);
        handle_connections(&rfds, &wfds);
    }

    free_listeners();
    free_connections();
}

static void
sig_handler(int sig) {
    switch(sig) {
        case(SIGHUP):
            sighup_received = 1;
            break;
        case(SIGINT):
        case(SIGTERM):
            running = 0;
    }
    signal(sig, sig_handler);
    /* Reinstall signal handler */
}
