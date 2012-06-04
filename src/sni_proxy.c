/*
 * Copyright (c) 2011 and 2012, Dustin Lundquist <dustin@null-ptr.net>
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
#include <fcntl.h>
#include <getopt.h>
#include <pwd.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "sni_proxy.h"
#include "server.h"


static void usage();
static void daemonize(const char *);


int
main(int argc, char **argv) {
    struct Config *config = NULL;
    const char *config_file = "/etc/sni_proxy.conf";
    int background_flag = 1;
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
    if (config == NULL) {
        fprintf(stderr, "Unable to load %s\n", config_file);
        return 1;
    }

    init_server(config);

    if (background_flag)
        daemonize(config->user ? config->user : DEFAULT_USERNAME);

    openlog(SYSLOG_IDENT, LOG_CONS, SYSLOG_FACILITY);

    run_server();

    free_config(config);

    return 0;
}

static void
daemonize(const char *username) {
    int i, fd0, fd1, fd2;
    pid_t pid;
    struct rlimit rl;
    struct passwd *user;
    struct stat sb;

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

    /* close all non socket file descriptors */
    for (i = sysconf(_SC_OPEN_MAX); i >= 0; i--) {
        if (fstat(i, &sb) == -1 || S_ISSOCK(sb.st_mode))
            continue;

        close(i);
    }

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
