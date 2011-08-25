#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <getopt.h>
#include <pwd.h>
#include <strings.h> /* bzero() */
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include "sni_proxy.h"
#include "config.h"
#include "server.h"


static void usage();
static void daemonize(const char *, const char *, int);


int
main(int argc, char **argv) {
    int opt, sockfd;
    int background_flag = 1;
    int tls_flag = 1;
    int port = 443;
    const char *config_file = "/etc/sni_proxy.conf";
    const char *bind_addr = "::";
    const char *user= "nobody";


    while ((opt = getopt(argc, argv, "fhb:p:c:u:")) != -1) {
        switch (opt) {
            case 'f': /* foreground */
                background_flag = 0;
                break;
            case 'h':
                tls_flag = 0;
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


    init_config(config_file);

    sockfd = init_server(bind_addr, port);
    if (sockfd < 0)
        return -1;

    if (background_flag)
        daemonize(argv[0], user, sockfd);

    openlog(SYSLOG_IDENT, LOG_CONS, SYSLOG_FACILITY);

    /* ignore SIGPIPE, or it will kill us */
    signal(SIGPIPE, SIG_IGN);

    run_server(sockfd, tls_flag);

    free_config();

    return 0;
}

static void
daemonize(const char *cmd, const char *username, int sockfd) {
    int i, fd0, fd1, fd2;
    pid_t pid;
    struct rlimit rl;
    struct passwd *user;

    user = getpwnam(username);
    if (user == NULL) {
        perror("getpwnam()");
        exit(1);
    }

    umask(0);

    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        perror("getrlimit()");
        exit(1);
    }

    if ((pid = fork()) < 0) {
        perror("fork()");
        exit(1);
    } else if (pid != 0) {
        exit(0);
    }

    if (chdir("/") < 0) {
        perror("chdir()");
        exit(1);
    }

    if (setsid() < 0) {
        perror("setsid()");
        exit(1);
    }


    for (i = sysconf(_SC_OPEN_MAX); i >= 0; i--)
        if (i != sockfd)
            close(i);

    fd0 = open("/dev/null", O_RDWR);
    fd1 = dup(fd0);
    fd2 = dup(fd0);

    openlog(cmd, LOG_CONS, LOG_DAEMON);
    if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
        fprintf(stderr, "Unexpected file descriptors\n");
        exit(2);
    }

    if (setgid(user->pw_gid) < 0) {
        perror("setgid()");
        exit(1);
    }

    if (setuid(user->pw_uid) < 0) {
        perror("setuid()");
        exit(1);
    }

    pid = fork();
    if (pid < 0) {
        perror("fork()");
        exit(1);
    } else if (pid > 0) {
        exit(0);
    }
}

static void
usage() {
    fprintf(stderr, "Usage: sni_proxy [-c <config>] [-f] [-b <address>] [-p <port>] [-u <user>]\n");
}
