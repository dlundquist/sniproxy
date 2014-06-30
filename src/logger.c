/*
 * Copyright (c) 2013, Dustin Lundquist <dustin@null-ptr.net>
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
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <assert.h>
#include "logger.h"

struct Logger {
    FILE *fd;
    int priority;
    int facility;
    const char *ident;
    int reference_count;
};

static struct Logger *default_logger = NULL;


static void free_logger(struct Logger *);
static struct Logger *new_fd_logger(FILE *);
static void init_default_logger();
static void vlog_msg(struct Logger *, int, const char *, va_list);
static void free_at_exit();
static int lookup_syslog_facility(const char *);


struct Logger *
new_syslog_logger(const char *ident, const char *facility) {
    struct Logger *logger = malloc(sizeof(struct Logger));
    if (logger != NULL) {
        logger->fd = NULL;
        logger->priority = LOG_DEBUG;
        logger->facility = lookup_syslog_facility(facility);
        logger->ident = NULL;
        logger->reference_count = 0;
        if (ident != NULL) {
            logger->ident = strdup(ident);
            if (logger->ident == NULL) {
                free(logger);
                return NULL;
            }
        }
    }

    return logger;
}

struct Logger *
new_file_logger(const char *filepath) {
    FILE *fd = fopen(filepath, "a");
    if (fd == NULL) {
        err("Failed to open new log file: %s", filepath);
        return NULL;
    }
    setvbuf(fd, NULL, _IOLBF, 0);

    return new_fd_logger(fd);
}

void
set_default_logger(struct Logger *new_logger) {
    logger_ref_put(default_logger);
    default_logger = logger_ref_get(new_logger);
}

void
set_logger_priority(struct Logger *logger, int priority) {
    logger->priority = priority;
}

void
logger_ref_put(struct Logger *logger) {
    if (logger == NULL)
        return;

    assert(logger->reference_count > 0);
    logger->reference_count--;
    if (logger->reference_count == 0)
        free_logger(logger);
}

struct Logger *
logger_ref_get(struct Logger *logger) {
    if (logger != NULL)
        logger->reference_count++;

    return logger;
}

static void
free_logger(struct Logger *logger) {
    if (logger == NULL)
        return;

    if (logger->fd) {
        fclose(logger->fd);
        logger->fd = NULL;
    }

    free(logger);
}

void
log_msg(struct Logger *logger, int priority, const char *format, ...) {
    va_list args;

    va_start(args, format);
    vlog_msg(logger, priority, format, args);
    va_end(args);
}

void
fatal(const char *format, ...) {
    va_list args;

    va_start(args, format);
    vlog_msg(default_logger, LOG_CRIT, format, args);
    va_end(args);

    exit(EXIT_FAILURE);
}

void
err(const char *format, ...) {
    va_list args;

    va_start(args, format);
    vlog_msg(default_logger, LOG_ERR, format, args);
    va_end(args);
}

void
warn(const char *format, ...) {
    va_list args;

    va_start(args, format);
    vlog_msg(default_logger, LOG_WARNING, format, args);
    va_end(args);
}

void
notice(const char *format, ...) {
    va_list args;

    va_start(args, format);
    vlog_msg(default_logger, LOG_NOTICE, format, args);
    va_end(args);
}

void
info(const char *format, ...) {
    va_list args;

    va_start(args, format);
    vlog_msg(default_logger, LOG_INFO, format, args);
    va_end(args);
}

void
debug(const char *format, ...) {
    va_list args;

    va_start(args, format);
    vlog_msg(default_logger, LOG_DEBUG, format, args);
    va_end(args);
}

static struct Logger *
new_fd_logger(FILE *fd) {
    assert(fd != NULL);

    struct Logger *logger = malloc(sizeof(struct Logger));
    if (logger != NULL) {
        logger->fd = fd;
        logger->priority = LOG_DEBUG;
        logger->facility = 0;
        logger->ident = NULL;
        logger->reference_count = 0;
    }

    return logger;
}

static void
vlog_msg(struct Logger *logger, int priority, const char *format, va_list args) {
    char buffer[1024];

    if (default_logger == NULL)
        init_default_logger();

    if (logger == NULL)
        return;

    if (priority > logger->priority)
        return;

    if (logger->fd) {
        /* file logger */
        time_t t = time(NULL);
        struct tm *tmp = localtime(&t);
        size_t len = strftime(buffer, sizeof(buffer), "%F %T ", tmp);
        vsnprintf(buffer + len, sizeof(buffer) - len, format, args);
        fprintf(logger->fd, "%s\n", buffer);
    } else {
        openlog(logger->ident, LOG_PID, logger->facility);
        vsyslog(priority, format, args);
        closelog();
    }
}

static void
init_default_logger() {
    struct Logger *logger = new_fd_logger(stderr);
    if (logger == NULL)
        return;

    atexit(free_at_exit);

    default_logger = logger_ref_get(logger);
}

static void
free_at_exit() {
    logger_ref_put(default_logger);
    default_logger = NULL;
}

static int
lookup_syslog_facility(const char *facility) {
    static const struct {
        const char *name;
        int number;
    } facilities[] = {
        { "auth",   LOG_AUTH },
        { "cron",   LOG_CRON },
        { "daemon", LOG_DAEMON },
        { "ftp",    LOG_FTP },
        { "local0", LOG_LOCAL0 },
        { "local1", LOG_LOCAL1 },
        { "local2", LOG_LOCAL2 },
        { "local3", LOG_LOCAL3 },
        { "local4", LOG_LOCAL4 },
        { "local5", LOG_LOCAL5 },
        { "local6", LOG_LOCAL6 },
        { "local7", LOG_LOCAL7 },
        { "mail",   LOG_MAIL },
        { "news",   LOG_NEWS },
        { "user",   LOG_USER },
        { "uucp",   LOG_UUCP },
    };

    for (size_t i = 0; i < sizeof(facilities) / sizeof(facilities[0]); i++)
        if(strncasecmp(facilities[i].name, facility, strlen(facility)) == 0)
            return facilities[i].number;

    /* fall back value */
    return LOG_USER;
}
