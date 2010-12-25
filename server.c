#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> /* close() */
#include <sys/select.h>
#include <strings.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "connection.h"
#include "server.h"
#include "backend.h"

#define BACKLOG 5

static void sig_handler(int);

static volatile int running; /* For signal handler */
static volatile int sighup_received; /* For signal handler */


size_t
parse_address(struct sockaddr_storage* addr, const char* address, int port) {

    memset(addr, 0, sizeof(struct sockaddr_storage));
    if (inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1) {
        ((struct sockaddr_in *)addr)->sin_family = AF_INET;
        ((struct sockaddr_in *)addr)->sin_port = htons(port);
        return sizeof(struct sockaddr_in);
    }

    memset(addr, 0, sizeof(struct sockaddr_storage));
    if (inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1) {
        ((struct sockaddr_in6 *)addr)->sin6_family = AF_INET6;
        ((struct sockaddr_in6 *)addr)->sin6_port = htons(port);
        return sizeof(struct sockaddr_in6);
    }

    return 0;
}

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
    int retval, maxfd;
    fd_set rfds;

    init_connections();
    running = 1;

    if (listen(sockfd, BACKLOG) < 0) {
        perror("ERROR listen();");
        return;
    }


    while (running) {
        maxfd = fd_set_connections(&rfds, sockfd);

        retval = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (retval < 0) {
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
}

static void
sig_handler(int sig) {
    switch(sig) {
        case(SIGHUP):
            sighup_received = 1;
            signal(sig, sig_handler);
            break;
        case(SIGINT):
        case(SIGTERM):
            running = 0;
    }
    /* Reinstall signal handler */
}
