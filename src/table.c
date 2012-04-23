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
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include "table.h"
#include "backend.h"

static void init_table(struct Table *);

struct Table *
new_table() {
    struct Table *table;

    table = malloc(sizeof(struct Table));
    if (table == NULL) {
        perror("malloc");
        return NULL;
    }

    table->name = NULL;
    STAILQ_INIT(&table->backends);

    return table;
}

int
accept_table_arg(struct Table *table, char *arg) {
    if (table->name == NULL) {
        table->name = strdup(arg);
        if (table->name == NULL) {
            perror("strdup");
            return -1;
        }
    } else {
        fprintf(stderr, "Unexpected table argument: %s\n", arg);
        return -1;
    }

    return 1;
}


void
add_table(struct Table_head *tables, struct Table *table) {
    SLIST_INSERT_HEAD(tables, table, entries);
}

void init_tables(struct Table_head *tables) {
    struct Table *iter;

    SLIST_FOREACH(iter, tables, entries)
        init_table(iter);
}

static void init_table(struct Table *table) {
    struct Backend *iter;
    
    STAILQ_FOREACH(iter, &table->backends, entries)
        init_backend(iter);
}

void
free_tables(struct Table_head *tables) {
    struct Table *iter;

    while ((iter = SLIST_FIRST(tables)) != NULL) {
        SLIST_REMOVE_HEAD(tables, entries);
        free_table(iter);
    }
}

struct Table *
lookup_table(const struct Table_head *tables, const char *name) {
    struct Table *iter;

    SLIST_FOREACH(iter, tables, entries) {
        if (name == NULL) {
            if (iter->name == NULL)
                return iter;
        } else if (iter->name) {
            if (strcmp(iter->name, name) == 0)
                return iter;
        }
    }

    return NULL;
}

void
remove_table(struct Table_head *tables, struct Table *table) {
    SLIST_REMOVE(tables, table, Table, entries);
    free_table(table);
}

int
lookup_table_server_socket(const struct Table *table, const char *hostname) {
    struct Backend *b;

    b = lookup_table_backend(table, hostname);
    if (b == NULL) {
        syslog(LOG_INFO, "No match found for %s", hostname);
        return -1;
    }

    return open_backend_socket(b, hostname);
}

void
print_table_config(FILE *file, struct Table *table) {
    struct Backend *backend;

    if (table->name == NULL)
        fprintf(file, "table {\n");
    else
        fprintf(file, "table %s {\n", table->name);

    STAILQ_FOREACH(backend, &table->backends, entries) {
        if (backend->port == 0)
            fprintf(file, "\t%s %s\n", backend->hostname, backend->address);
        else 
            fprintf(file, "\t%s %s %d\n", backend->hostname, backend->address, backend->port);
    }
    fprintf(file, "}\n\n");
}

void
free_table(struct Table *table) {
    struct Backend *iter;

    while ((iter = STAILQ_FIRST(&table->backends)) != NULL)
        remove_backend(&table->backends, iter);
    free(table->name);
    free(table);
}
