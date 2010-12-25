#include <stdio.h>
#include <stdlib.h>
#include <strings.h> /* bzero() */
#include <getopt.h>
#include "connection.h"
#include "tls.h"
#include "util.h"
#include "backend.h"
#include "server.h"


static void usage();

int
main(int argc, char **argv) {
    int opt, sockfd;
    int background_flag = 1;
    int port = 443;
    const char *config_file = "/etc/sni_proxy.conf";
    const char *bind_addr= "::";
    const char *user= "nobody";


    while ((opt = getopt(argc, argv, "fb:p:c:u:")) != -1) {
        switch (opt) {
            case 'f': /* foreground */
                background_flag = 0;
                break;
            case 'b':
                bind_addr = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'u':
                user = optarg;
                break;
            default: 
                usage();
                exit(EXIT_FAILURE);
        }
    }


    init_backends(config_file);

    sockfd = init_server(bind_addr, port);
    if (sockfd < 0)
        return -1;

    /* TODO drop privileges */

    run_server(sockfd);

    return 0;
}


static void usage() {
    fprintf(stderr, "Usage: sni_proxy [-c <config_file] [-p <port>]\n");
}
