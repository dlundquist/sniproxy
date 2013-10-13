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
#include <syslog.h>
#include "cfg_parser.h"
#include "config.h"


static int accept_username(struct Config *, char *);
static int accept_pidfile(struct Config *, char *);
static int end_listener_stanza(struct Config *, struct Listener *);
static int end_table_stanza(struct Config *, struct Table *);
static int end_backend(struct Table *, struct Backend *);


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
init_config(const char *filename) {
    FILE *file;
    struct Config *config;
    int i;

    config = malloc(sizeof(struct Config));
    if (config == NULL) {
        perror("malloc()");
        return NULL;
    }

    config->filename = NULL;
    config->user = NULL;
    config->pidfile = NULL;
    SLIST_INIT(&config->listeners);
    SLIST_INIT(&config->tables);

    config->filename = strdup(filename);
    if (config->filename == NULL) {
        perror("malloc()");
        free_config(config);
        return NULL;
    }


    file = fopen(config->filename, "r");
    if (file == NULL) {
        perror("unable to open config file");
        free_config(config);
        return NULL;
    }

    if (parse_config((void *)config, file, global_grammar) <= 0) {
        uint64_t whence = ftell(file);
        char buffer[256];

        fprintf(stderr, "error parsing %s at %ld near:\n", filename, whence);
        fseek(file, -20, SEEK_CUR);
        for (i = 0; i < 5; i++)
            fprintf(stderr, "%ld\t%s", ftell(file),
                    fgets(buffer, sizeof(buffer), file));

        free_config(config);
        config = NULL;
    }

    fclose(file);

    return(config);
}

void
free_config(struct Config *config) {
    free(config->filename);
    free(config->user);
    free(config->pidfile);

    free_listeners(&config->listeners);

    free_tables(&config->tables);

    free(config);
}

int
reload_config(struct Config *config) {
    /* TODO */

    /* Open client connections have references the listener they connected
     * to, so we can't simply load a new configuration and discard the old
     * one.
     */
    syslog(LOG_INFO, "reload of %s not supported, see TODO", config->filename);

    return 0;
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
            perror("malloc:");
            return -1;
        }

        return 1;
}

static int
accept_pidfile(struct Config *config, char *pidfile) {
        config->pidfile = strdup(pidfile);
        if (config->pidfile == NULL) {
            perror("malloc:");
            return -1;
        }

        return 1;
}

static int
end_listener_stanza(struct Config *config, struct Listener *listener) {
    if (valid_listener(listener) <= 0) {
        fprintf(stderr, "Invalid listener\n");
        print_listener_config(stderr, listener);
        free_listener(listener);
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

