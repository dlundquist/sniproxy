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
#include <errno.h>
#include <assert.h>
#include "cfg_parser.h"
#include "config.h"
#include "logger.h"


struct LoggerBuilder {
    const char *filename;
    const char *syslog_facility;
    int priority;
};

static int accept_username(struct Config *, char *);
static int accept_groupname(struct Config *, char *);
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
static struct ResolverConfig *new_resolver_config();
static int accept_resolver_nameserver(struct ResolverConfig *, char *);
static int accept_resolver_search(struct ResolverConfig *, char *);
static int accept_resolver_mode(struct ResolverConfig *, char *);
static int end_resolver_stanza(struct Config *, struct ResolverConfig *);
static inline size_t string_vector_len(char **);
static int append_to_string_vector(char ***, const char *) __attribute__((nonnull(1)));
static void free_string_vector(char **);
static void print_resolver_config(FILE *, struct ResolverConfig *);


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

struct Keyword resolver_stanza_grammar[] = {
    { "nameserver",
            NULL,
            (int(*)(void *, char *))accept_resolver_nameserver,
            NULL,
            NULL},
    { "search",
            NULL,
            (int(*)(void *, char *))accept_resolver_search,
            NULL,
            NULL},
    { "mode",
            NULL,
            (int(*)(void *, char *))accept_resolver_mode,
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
    { "reuseport",
            NULL,
            (int(*)(void *, char *))accept_listener_reuseport,
            NULL,
            NULL},
    { "ipv6_v6only",
            NULL,
            (int(*)(void *, char *))accept_listener_ipv6_v6only,
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
    { "groupname",
            NULL,
            (int(*)(void *, char *))accept_groupname,
            NULL,
            NULL},
    { "pidfile",
            NULL,
            (int(*)(void *, char *))accept_pidfile,
            NULL,
            NULL},
    { "resolver",
            (void *(*)())new_resolver_config,
            NULL,
            resolver_stanza_grammar,
            (int(*)(void *, void *))end_resolver_stanza},
    { "error_log",
            (void *(*)())new_logger_builder,
            NULL,
            logger_stanza_grammar,
            (int(*)(void *, void *))end_error_logger_stanza},
    { "access_log",
            (void *(*)())new_logger_builder,
            NULL,
            logger_stanza_grammar,
            (int(*)(void *, void *))end_global_access_logger_stanza},
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

static const char *resolver_mode_names[] = {
    "DEFAULT",
    "ipv4_only",
    "ipv6_only",
    "ipv4_first",
    "ipv6_first",
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
    config->group = NULL;
    config->pidfile = NULL;
    config->access_log = NULL;
    config->resolver.nameservers = NULL;
    config->resolver.search = NULL;
    config->resolver.mode = 0;
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

    /* Listeners without access logger defined used global access log */
    if (config != NULL && config->access_log != NULL) {
        struct Listener *listener;
        SLIST_FOREACH(listener, &config->listeners, entries) {
            if (listener->access_log == NULL) {
                listener->access_log = logger_ref_get(config->access_log);
            }
        }
    }

    return(config);
}

void
free_config(struct Config *config, struct ev_loop *loop) {
    free(config->filename);
    free(config->user);
    free(config->group);
    free(config->pidfile);

    free_string_vector(config->resolver.nameservers);
    config->resolver.nameservers = NULL;
    free_string_vector(config->resolver.search);
    config->resolver.search = NULL;

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

    print_resolver_config(file, &config->resolver);

    SLIST_FOREACH(listener, &config->listeners, entries) {
        print_listener_config(file, listener);
    }

    SLIST_FOREACH(table, &config->tables, entries) {
        print_table_config(file, table);
    }
}

static int
accept_username(struct Config *config, char *username) {
    if (config->user != NULL) {
        err("Duplicate username: %s", username);
        return 0;
    }
    config->user = strdup(username);
    if (config->user == NULL) {
        err("%s: strdup", __func__);
        return -1;
    }

    return 1;
}

static int
accept_groupname(struct Config *config, char *groupname) {
    if (config->group != NULL) {
        err("Duplicate groupname: %s", groupname);
        return 0;
    }
    config->group = strdup(groupname);
    if (config->group == NULL) {
        err("%s: strdup", __func__);
        return -1;
    }

    return 1;
}

static int
accept_pidfile(struct Config *config, char *pidfile) {
    if (config->pidfile != NULL) {
        err("Duplicate pidfile: %s", pidfile);
        return 0;
    }
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
        assert(listener->reference_count == 1);
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
        if (strncasecmp(priorities[i].name, priority, strlen(priority)) == 0) {
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

static struct ResolverConfig *
new_resolver_config() {
    struct ResolverConfig *resolver = malloc(sizeof(struct ResolverConfig));

    if (resolver != NULL) {
        resolver->nameservers = NULL;
        resolver->search = NULL;
        resolver->mode = 0;
    }

    return resolver;
}

static size_t
string_vector_len(char **vector) {
    size_t len = 0;
    while (vector != NULL && *vector++ != NULL)
        len++;

    return len;
}

static int
append_to_string_vector(char ***vector_ptr, const char *string) {
    char **vector = *vector_ptr;

    size_t len = string_vector_len(vector);
    vector = realloc(vector, (len + 2) * sizeof(char *));
    if (vector == NULL) {
        err("%s: realloc", __func__);
        return -errno;
    }

    *vector_ptr = vector;

    vector[len] = strdup(string);
    if (vector[len] == NULL) {
        err("%s: strdup", __func__);
        return -errno;
    }
    vector[len + 1] = NULL;

    return len + 1;
}

static void
free_string_vector(char **vector) {
    for (int i = 0; vector != NULL && vector[i] != NULL; i++) {
        free(vector[i]);
        vector[i] = NULL;
    }

    free(vector);
}

static int
accept_resolver_nameserver(struct ResolverConfig *resolver, char *nameserver) {
    /* Validate address is a valid IP */
    struct Address *ns_address = new_address(nameserver);
    if (!address_is_sockaddr(ns_address)) {
        free(ns_address);
        return -1;
    }
    free(ns_address);

    return append_to_string_vector(&resolver->nameservers, nameserver);
}

static int
accept_resolver_search(struct ResolverConfig *resolver, char *search) {
    struct Address *search_address = new_address(search);
    if (!address_is_hostname(search_address)) {
        free(search_address);
        return -1;
    }
    free(search_address);

    return append_to_string_vector(&resolver->search, search);
}

static int
accept_resolver_mode(struct ResolverConfig *resolver, char *mode) {
    for (size_t i = 0; i < sizeof(resolver_mode_names) / sizeof(resolver_mode_names[0]); i++)
        if (strncasecmp(resolver_mode_names[i], mode, strlen(mode)) == 0) {
            resolver->mode = i;
            return 1;
        }

    return -1;
}

static int
end_resolver_stanza(struct Config *config, struct ResolverConfig *resolver) {
    config->resolver = *resolver;
    free(resolver);

    return 1;
}

static void
print_resolver_config(FILE *file, struct ResolverConfig *resolver) {
    fprintf(file, "resolver {\n");

    for (int i = 0; resolver->nameservers != NULL && resolver->nameservers[i] != NULL; i++)
        fprintf(file, "\tnameserver %s\n", resolver->nameservers[i]);

    for (int i = 0; resolver->search != NULL && resolver->search[i] != NULL; i++)
        fprintf(file, "\tsearch %s\n", resolver->search[i]);

    fprintf(file, "\tmode %s\n", resolver_mode_names[resolver->mode]);

    fprintf(file, "}\n\n");
}
