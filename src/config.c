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
#include <stdint.h>
#include <string.h>
#include "cfg_parser.h"
#include "config.h"
#include "logger.h"


struct LoggerBuilder {
    const char *filename;
    const char *syslog_facility;
    int priority;
};

static int accept_username(struct Config *, char *);
static int accept_pidfile(struct Config *, char *);
static int end_listener_stanza(struct Config *, struct Listener *);
static int end_table_stanza(struct Config *, struct Table *);
static int end_backend(struct Table *, struct Backend *);
static struct LoggerBuilder *new_logger_builder();
static int accept_logger_filename(struct LoggerBuilder *, char *);
static int accept_logger_syslog_facility(struct LoggerBuilder *, char *);
static int accept_logger_priority(struct LoggerBuilder *, char *);
static int end_error_logger_stanza(struct Config *, struct LoggerBuilder *);
static int end_global_access_logger_stanza(struct Config *, struct LoggerBuilder *);
static int end_listener_access_logger_stanza(struct Listener *, struct LoggerBuilder *);


static struct Keyword logger_stanza_grammar[] = {
    { "filename",
            NULL,
            (int(*)(void *, char *))accept_logger_filename,
            NULL,
            NULL},
    { "syslog",
            NULL,
            (int(*)(void *, char *))accept_logger_syslog_facility,
            NULL,
            NULL},
    { "priority",
            NULL,
            (int(*)(void *, char *))accept_logger_priority,
            NULL,
            NULL},
    { NULL, NULL, NULL, NULL, NULL },
};

struct Keyword listener_stanza_grammar[] = {
    { "protocol",
            NULL,
            (int(*)(void *, char *))accept_listener_protocol,
            NULL,
            NULL},
    { "table",
            NULL,
            (int(*)(void *, char *))accept_listener_table_name,
            NULL,
            NULL},
    { "fallback",
            NULL,
            (int(*)(void *, char *))accept_listener_fallback_address,
            NULL,
            NULL},
    { "source",
            NULL,
            (int(*)(void *, char *))accept_listener_source_address,
            NULL,
            NULL},
    { "access_log",
            (void *(*)())new_logger_builder,
            (int(*)(void *, char *))accept_logger_filename,
            logger_stanza_grammar,
            (int(*)(void *, void *))end_listener_access_logger_stanza},
    { "bad_requests",
            NULL,
            (int(*)(void *, char *))accept_listener_bad_request_action,
            NULL,
            NULL},
    { NULL, NULL, NULL, NULL, NULL }
};

static struct Keyword table_stanza_grammar[] = {
    { NULL,
            (void *(*)())new_backend,
            (int(*)(void *, char *))accept_backend_arg,
            NULL,
            (int(*)(void *, void *))end_backend},
};

static struct Keyword global_grammar[] = {
    { "username",
            NULL,
            (int(*)(void *, char *))accept_username,
            NULL,
            NULL},
    { "pidfile",
            NULL,
            (int(*)(void *, char *))accept_pidfile,
            NULL,
            NULL},
    { "error_log",
            (void *(*)())new_logger_builder,
            NULL,
            logger_stanza_grammar,
            (int(*)(void *, void *))end_error_logger_stanza},
/*
 * { "access_log",
 *          (void *(*)())new_logger_builder,
 *          NULL,
 *          logger_stanza_grammar,
 *          (int(*)(void *, void *))end_global_access_logger_stanza},
 */
    { "listener",
            (void *(*)())new_listener,
            (int(*)(void *, char *))accept_listener_arg,
            listener_stanza_grammar,
            (int(*)(void *, void *))end_listener_stanza},
    { "table",
            (void *(*)())new_table,
            (int(*)(void *, char *))accept_table_arg,
            table_stanza_grammar,
            (int(*)(void *, void *))end_table_stanza},
    { NULL, NULL, NULL, NULL, NULL }
};


struct Config *
init_config(const char *filename, struct ev_loop *loop) {
    struct Config *config = malloc(sizeof(struct Config));
    if (config == NULL) {
        err("%s: malloc", __func__);
        return NULL;
    }

    config->filename = NULL;
    config->user = NULL;
    config->pidfile = NULL;
    config->access_log = NULL;
    SLIST_INIT(&config->listeners);
    SLIST_INIT(&config->tables);

    config->filename = strdup(filename);
    if (config->filename == NULL) {
        err("%s: strdup", __func__);
        free_config(config, loop);
        return NULL;
    }


    FILE *file = fopen(config->filename, "r");
    if (file == NULL) {
        err("%s: unable to open configuration file: %s", __func__, config->filename);
        free_config(config, loop);
        return NULL;
    }

    if (parse_config(config, file, global_grammar) <= 0) {
        intmax_t whence = ftell(file);
        char buffer[256];

        err("error parsing %s at %jd near:", filename, whence);
        fseek(file, -20, SEEK_CUR);
        for (int i = 0; i < 5; i++)
            err("%ld\t%s", ftell(file), fgets(buffer, sizeof(buffer), file));

        free_config(config, loop);
        config = NULL;
    }

    fclose(file);

    return(config);
}

void
free_config(struct Config *config, struct ev_loop *loop) {
    free(config->filename);
    free(config->user);
    free(config->pidfile);

    logger_ref_put(config->access_log);
    free_listeners(&config->listeners, loop);
    free_tables(&config->tables);

    free(config);
}

void
reload_config(struct Config *config, struct ev_loop *loop) {
    notice("reloading configuration from %s", config->filename);

    struct Config *new_config = init_config(config->filename, loop);
    if (new_config == NULL) {
        err("failed to reload %s", config->filename);
        return;
    }

    /* update access_log */
    logger_ref_put(config->access_log);
    config->access_log = logger_ref_get(new_config->access_log);

    reload_tables(&config->tables, &new_config->tables);

    listeners_reload(&config->listeners, &new_config->listeners,
            &config->tables, loop);

    free_config(new_config, loop);
}

void
print_config(FILE *file, struct Config *config) {
    struct Listener *listener = NULL;
    struct Table *table = NULL;

    if (config->filename)
        fprintf(file, "# Config loaded from %s\n\n", config->filename);

    if (config->user)
        fprintf(file, "username %s\n\n", config->user);

    if (config->pidfile)
        fprintf(file, "pidfile %s\n\n", config->pidfile);

    SLIST_FOREACH(listener, &config->listeners, entries) {
        print_listener_config(file, listener);
    }

    SLIST_FOREACH(table, &config->tables, entries) {
        print_table_config(file, table);
    }
}

static int
accept_username(struct Config *config, char *username) {
    config->user = strdup(username);
    if (config->user == NULL) {
        err("%s: strdup", __func__);
        return -1;
    }

    return 1;
}

static int
accept_pidfile(struct Config *config, char *pidfile) {
    config->pidfile = strdup(pidfile);
    if (config->pidfile == NULL) {
        err("%s: strdup", __func__);
        return -1;
    }

    return 1;
}

static int
end_listener_stanza(struct Config *config, struct Listener *listener) {
    if (valid_listener(listener) <= 0) {
        err("Invalid listener");
        print_listener_config(stderr, listener);

        /* free listener */
        listener_ref_get(listener);
        listener_ref_put(listener);

        return -1;
    }

    add_listener(&config->listeners, listener);

    return 1;
}

static int
end_table_stanza(struct Config *config, struct Table *table) {
    /* TODO check table */

    add_table(&config->tables, table);

    return 1;
}

static int
end_backend(struct Table *table, struct Backend *backend) {
    /* TODO check backend */

    add_backend(&table->backends, backend);

    return 1;
}

static struct LoggerBuilder *
new_logger_builder() {
    struct LoggerBuilder *lb = malloc(sizeof(struct LoggerBuilder));
    if (lb == NULL) {
        err("%s: malloc", __func__);
        return NULL;
    }

    lb->filename = NULL;
    lb->syslog_facility = NULL;
    lb->priority = LOG_NOTICE;

    return lb;
}

static int
accept_logger_filename(struct LoggerBuilder *lb, char *filename) {
    lb->filename = strdup(filename);
    if (lb->filename == NULL) {
        err("%s: strdup", __func__);
        return -1;
    }

    return 1;
}

static int
accept_logger_syslog_facility(struct LoggerBuilder *lb, char *facility) {
    lb->syslog_facility = strdup(facility);
    if (lb->syslog_facility == NULL) {
        err("%s: strdup", __func__);
        return -1;
    }

    return 1;
}

static int
accept_logger_priority(struct LoggerBuilder *lb, char *priority) {
    const struct {
        const char *name;
        int priority;
    } priorities[] = {
        { "emergency",  LOG_EMERG },
        { "alert",      LOG_ALERT },
        { "critical",   LOG_CRIT },
        { "error",      LOG_ERR },
        { "warning",    LOG_WARNING },
        { "notice",     LOG_NOTICE },
        { "info",       LOG_INFO },
        { "debug",      LOG_DEBUG },
    };

    for (size_t i = 0; i < sizeof(priorities) / sizeof(priorities[0]); i++)
        if(strncasecmp(priorities[i].name, priority, strlen(priority)) == 0) {
            lb->priority = priorities[i].priority;
            return 1;
        }

    return -1;
}

static int
end_error_logger_stanza(struct Config *config __attribute__ ((unused)), struct LoggerBuilder *lb) {
    struct Logger *logger = NULL;

    if (lb->filename != NULL && lb->syslog_facility == NULL)
        logger = new_file_logger(lb->filename);
    else if (lb->syslog_facility != NULL && lb->filename == NULL)
        logger = new_syslog_logger(lb->syslog_facility);
    else
        err("Logger can not be both file logger and syslog logger");

    if (logger == NULL) {
        free((char *)lb->filename);
        free((char *)lb->syslog_facility);
        free(lb);
        return -1;
    }

    set_logger_priority(logger, lb->priority);
    set_default_logger(logger);

    free((char *)lb->filename);
    free((char *)lb->syslog_facility);
    free(lb);
    return 1;
}

static int __attribute__((unused))
end_global_access_logger_stanza(struct Config *config, struct LoggerBuilder *lb) {
    struct Logger *logger = NULL;

    if (lb->filename != NULL && lb->syslog_facility == NULL)
        logger = new_file_logger(lb->filename);
    else if (lb->syslog_facility != NULL && lb->filename == NULL)
        logger = new_syslog_logger(lb->syslog_facility);
    else
        err("Logger can not be both file logger and syslog logger");

    if (logger == NULL) {
        free((char *)lb->filename);
        free((char *)lb->syslog_facility);
        free(lb);
        return -1;
    }

    set_logger_priority(logger, lb->priority);
    logger_ref_put(config->access_log);
    config->access_log = logger_ref_get(logger);

    free((char *)lb->filename);
    free((char *)lb->syslog_facility);
    free(lb);
    return 1;
}

static int
end_listener_access_logger_stanza(struct Listener *listener, struct LoggerBuilder *lb) {
    struct Logger *logger = NULL;

    if (lb->filename != NULL && lb->syslog_facility == NULL)
        logger = new_file_logger(lb->filename);
    else if (lb->syslog_facility != NULL && lb->filename == NULL)
        logger = new_syslog_logger(lb->syslog_facility);
    else
        err("Logger can not be both file logger and syslog logger");

    if (logger == NULL) {
        free((char *)lb->filename);
        free((char *)lb->syslog_facility);
        free(lb);
        return -1;
    }

    set_logger_priority(logger, lb->priority);
    logger_ref_put(listener->access_log);
    listener->access_log = logger_ref_get(logger);

    free((char *)lb->filename);
    free((char *)lb->syslog_facility);
    free(lb);
    return 1;
}
