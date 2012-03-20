#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <getopt.h>
#include <pwd.h>
// #include <strings.h> /* bzero() */
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
static void daemonize(const char *, int);


int
main(int argc, char **argv) {
    struct Config *config = NULL;
    const char *config_file = "/etc/sni_proxy.conf";
    int background_flag = 1;
    int fd_count;
    int opt;


    while ((opt = getopt(argc, argv, "fc:")) != -1) {
        switch (opt) {
            case 'c':
                config_file = optarg;
                break;
            case 'f': /* foreground */
                background_flag = 0;
                break;
            default: 
                usage();
                exit(EXIT_FAILURE);
        }
    }

    config = init_config(config_file);

    init_server(config);

    if (background_flag)
        daemonize(config->user, 5);

    openlog(SYSLOG_IDENT, LOG_CONS, SYSLOG_FACILITY);

    run_server();

    free_config(config);

    return 0;
}

static void
daemonize(const char *username, int sockfd) {
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
    fprintf(stderr, "Usage: sni_proxy [-c <config>] [-f]\n");
}
