/*
 * Copyright (c) 2011-2014, Dustin Lundquist <dustin@null-ptr.net>
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
#include <signal.h>
#include <errno.h>
#include <ev.h>
#include "binder.h"
#include "config.h"
#include "connection.h"
#include "listener.h"
#include "resolv.h"
#include "logger.h"


static void usage();
static void daemonize(void);
static void write_pidfile(const char *, pid_t);
static void set_limits(rlim_t);
static void drop_perms(const char* username, const char* groupname);
static void perror_exit(const char *);
static void signal_cb(struct ev_loop *, struct ev_signal *, int revents);


static const char *sniproxy_version = PACKAGE_VERSION;
static const char *default_username = "daemon";
static struct Config *config;
static struct ev_signal sighup_watcher;
static struct ev_signal sigusr1_watcher;
static struct ev_signal sigint_watcher;
static struct ev_signal sigterm_watcher;


int
main(int argc, char **argv) {
    const char *config_file = "/etc/sniproxy.conf";
    int background_flag = 1;
    rlim_t max_nofiles = 65536;
    int opt;

    while ((opt = getopt(argc, argv, "fc:n:V")) != -1) {
        switch (opt) {
            case 'c':
                config_file = optarg;
                break;
            case 'f': /* foreground */
                background_flag = 0;
                break;
            case 'n':
                max_nofiles = strtoul(optarg, NULL, 10);
                break;
            case 'V':
                printf("sniproxy %s\n", sniproxy_version);
                return EXIT_SUCCESS;
            default:
                usage();
                return EXIT_FAILURE;
        }
    }

    config = init_config(config_file, EV_DEFAULT);
    if (config == NULL) {
        fprintf(stderr, "Unable to load %s\n", config_file);
        usage();
        return EXIT_FAILURE;
    }

    /* ignore SIGPIPE, or it will kill us */
    signal(SIGPIPE, SIG_IGN);

    if (background_flag) {
        if (config->pidfile != NULL)
            remove(config->pidfile);

        daemonize();


        if (config->pidfile != NULL)
            write_pidfile(config->pidfile, getpid());
    }

    start_binder();

    set_limits(max_nofiles);

    init_listeners(&config->listeners, &config->tables, EV_DEFAULT);

    /* Drop permissions only when we can */
    drop_perms(config->user ? config->user : default_username, config->group);

    ev_signal_init(&sighup_watcher, signal_cb, SIGHUP);
    ev_signal_init(&sigusr1_watcher, signal_cb, SIGUSR1);
    ev_signal_init(&sigint_watcher, signal_cb, SIGINT);
    ev_signal_init(&sigterm_watcher, signal_cb, SIGTERM);
    ev_signal_start(EV_DEFAULT, &sighup_watcher);
    ev_signal_start(EV_DEFAULT, &sigusr1_watcher);
    ev_signal_start(EV_DEFAULT, &sigint_watcher);
    ev_signal_start(EV_DEFAULT, &sigterm_watcher);

    resolv_init(EV_DEFAULT, config->resolver.nameservers,
            config->resolver.search, config->resolver.mode);

    init_connections();

    ev_run(EV_DEFAULT, 0);

    free_connections(EV_DEFAULT);
    resolv_shutdown(EV_DEFAULT);

    free_config(config, EV_DEFAULT);

    stop_binder();

    return 0;
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
    else if (pid != 0)
        exit(EXIT_SUCCESS);

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

    ev_default_fork();

    return;
}

/**
 * Raise file handle limit to reasonable level
 * At some point we should make this a config parameter
 */
static void
set_limits(rlim_t max_nofiles) {
    struct rlimit fd_limit = {
        .rlim_cur = max_nofiles,
        .rlim_max = max_nofiles,
    };

    int result = setrlimit(RLIMIT_NOFILE, &fd_limit);
    if (result < 0)
        warn("Failed to set file handle limit: %s", strerror(errno));
}

static void
drop_perms(const char *username, const char *groupname) {
    /* check if we are already an unprivileged user */
    if (getuid() != 0)
        return;

    errno = 0;
    struct passwd *user = getpwnam(username);
    if (errno)
        fatal("getpwnam(): %s", strerror(errno));
    else if (user == NULL)
        fatal("getpwnam(): user %s does not exist", username);

    gid_t gid = user->pw_gid;

    if (groupname != NULL) {
      errno = 0;
      struct group *group = getgrnam(groupname);
      if (errno)
        fatal("getgrnam(): %s", strerror(errno));
      else if (group == NULL)
        fatal("getgrnam(): group %s does not exist", groupname);

      gid = group->gr_gid;
    }

    /* drop any supplementary groups */
    if (setgroups(1, &gid) < 0)
        fatal("setgroups(): %s", strerror(errno));

    /* set the main gid */
    if (setgid(gid) < 0)
        fatal("setgid(): %s", strerror(errno));

    if (setuid(user->pw_uid) < 0)
        fatal("setuid(): %s", strerror(errno));
}

static void
perror_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void
usage() {
    fprintf(stderr, "Usage: sniproxy [-c <config>] [-f] [-n <max file descriptor limit>] [-V]\n");
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

static void
signal_cb(struct ev_loop *loop, struct ev_signal *w, int revents) {
    if (revents & EV_SIGNAL) {
        switch (w->signum) {
            case SIGHUP:
                reopen_loggers();
                reload_config(config, loop);
                break;
            case SIGUSR1:
                print_connections();
                break;
            case SIGINT:
            case SIGTERM:
                ev_unloop(loop, EVUNLOOP_ALL);
        }
    }
}
