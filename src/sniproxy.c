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
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include "sniproxy.h"
#include "config.h"
#include "server.h"
#include "logger.h"


static void usage();
static void perror_exit(const char *);
static void daemonize(void);
static void set_limits(int);
static void drop_perms(const char* username);
static void write_pidfile(const char *, pid_t);

int
main(int argc, char **argv) {
    struct Config *config = NULL;
    const char *config_file = "/etc/sniproxy.conf";
    int background_flag = 1;
    int max_nofiles = 65536;
    int opt;

    while ((opt = getopt(argc, argv, "fc:n:")) != -1) {
        switch (opt) {
            case 'c':
                config_file = optarg;
                break;
            case 'f': /* foreground */
                background_flag = 0;
                break;
            case 'n':
                max_nofiles = atoi(optarg);
                break;
            default:
                usage();
                exit(EXIT_FAILURE);
        }
    }

    config = init_config(config_file);
    if (config == NULL) {
        fprintf(stderr, "Unable to load %s\n", config_file);
        usage();
        return 1;
    }

    init_server(config);

    if (background_flag) {
        if (config->pidfile != NULL)
            remove(config->pidfile);

        daemonize();

        if (config->pidfile != NULL) {
            write_pidfile(config->pidfile, getpid());
        }
    }

    set_limits(max_nofiles);

    /* Drop permissions only when we can */
    drop_perms(config->user ? config->user : DEFAULT_USERNAME);

    run_server();

    free_config(config);

    return 0;
}

static void
perror_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void
daemonize(void) {
#ifdef HAVE_DAEMON
    if (daemon(0,0) < 0)
        perror_exit("daemon()");
#else
    pid_t pid;

    /* daemon(0,0) part */
    pid = fork();
    if (pid < 0)
        perror_exit("fork()");
    else if (pid != 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0)
        perror_exit("setsid()");

    if (chdir("/") < 0)
        perror_exit("chdir()");

    if (freopen("/dev/null", "r", stdin) == NULL)
        perror_exit("freopen(stdin)");

    if (freopen("/dev/null", "a", stdout) == NULL)
        perror_exit("freopen(stdout)");

    if (freopen("/dev/null", "a", stderr) == NULL)
        perror_exit("freopen(stderr)");

    pid = fork();
    if (pid < 0)
        perror_exit("fork()");
    else if (pid != 0)
        exit(EXIT_SUCCESS);
#endif

    /* local part */
    umask(022);
    signal(SIGHUP, SIG_IGN);

    return;
}

/**
 * Raise file handle limit to reasonable level
 * At some point we should make this a config parameter
 */
static void
set_limits(int max_nofiles) {
    struct rlimit fd_limit = {
        .rlim_cur = max_nofiles,
        .rlim_max = max_nofiles,
    };

    int result = setrlimit(RLIMIT_NOFILE, &fd_limit);
    if (result < 0)
        warn("Failed to set file handle limit: %s", strerror(errno));
}

static void
drop_perms(const char *username) {
    struct passwd *user;

    if (getuid() != 0)
        return;

    user = getpwnam(username);
    if (user == NULL)
        perror_exit("getpwnam()");

    /* drop any supplementary groups */
    if (setgroups(1, &user->pw_gid) < 0)
        perror_exit("setgroups()");

    if (setgid(user->pw_gid) < 0)
        perror_exit("setgid()");

    if (setuid(user->pw_uid) < 0)
        perror_exit("setuid()");

    return;
}

static void
usage() {
    fprintf(stderr, "Usage: sniproxy [-c <config>] [-f] [-n <max file descriptor limit>\n");
}

static void
write_pidfile(const char *path, pid_t pid) {
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror("fopen");
        return;
    }

    fprintf(fp, "%d\n", pid);

    fclose(fp);
}
