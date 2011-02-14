#include <stdio.h>
#include <unistd.h> /* close() */
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include "server.h"
#include "connection.h"
#include "config.h"
#include "util.h"

#define BACKLOG 5

static void sig_handler(int);

static volatile int running; /* For signal handler */
static volatile int sighup_received; /* For signal handler */


int
init_server(const char* address, int port) {
    struct sockaddr_storage addr;
    int sockfd;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGHUP, sig_handler);

    if (parse_address(&addr, address, port) == 0) {
        perror("Error parsing address");
        return -1;
    }

    sockfd = socket(addr.ss_family, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        return -1;
    }

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("ERROR on binding");
        return -1;
    }
    return sockfd;
}

void
run_server(int sockfd) {
    int maxfd;
    fd_set rfds;

    init_connections();
    running = 1;

    if (listen(sockfd, BACKLOG) < 0) {
        perror("ERROR listen();");
        return;
    }


    while (running) {
        maxfd = fd_set_connections(&rfds, sockfd);

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


        if (FD_ISSET (sockfd, &rfds))
            accept_connection(sockfd);

        handle_connections(&rfds);
    }

    if (close(sockfd) < 0)
        perror("close()");

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
