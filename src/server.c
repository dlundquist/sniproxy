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
