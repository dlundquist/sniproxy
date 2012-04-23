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
#include <ctype.h>
#include <syslog.h>
#include "table.h"
#include "backend.h"


static SLIST_HEAD(, Table) tables;


static void free_table(struct Table *);


void
init_tables() {
    SLIST_INIT(&tables);
}

void
free_tables() {
    struct Table *iter;

    while ((iter = SLIST_FIRST(&tables)) != NULL) {
        SLIST_REMOVE_HEAD(&tables, entries);
        free_table(iter);
    }
}

struct Table *
add_table(const char *name) {
    struct Table *table;

    table = calloc(1, sizeof(struct Table));
    if (table == NULL) {
        syslog(LOG_CRIT, "calloc failed");
        return NULL;
    }
    table->name = strdup(name);
    if (table->name == NULL) {
        syslog(LOG_CRIT, "strdup failed");
        free(table);
        return NULL;
    }
    STAILQ_INIT(&table->backends);

    SLIST_INSERT_HEAD(&tables, table, entries);
   
    return table;
}

struct Table *
lookup_table(const char *name) {
    struct Table *iter;

    SLIST_FOREACH(iter, &tables, entries) {
        if (strcmp(iter->name, name) == 0)
            return iter;
    }

    return NULL;
}

void
remove_table(struct Table *table) {
    SLIST_REMOVE(&tables, table, Table, entries);
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

static void
free_table(struct Table *table) {
    struct Backend *iter;

    while ((iter = STAILQ_FIRST(&table->backends)) != NULL)
        remove_backend(&table->backends, iter);
    free(table->name);
    free(table);
}
