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
#include <errno.h>
#include <assert.h>
#include "table.h"
#include "backend.h"
#include "address.h"
#include "logger.h"


static void free_table(struct Table *);


static inline struct Backend *
table_lookup_backend(const struct Table *table, const char *name, size_t name_len) {
    return lookup_backend(&table->backends, name, name_len);
}

static inline void __attribute__((unused))
remove_table_backend(struct Table *table, struct Backend *backend) {
    remove_backend(&table->backends, backend);
}


struct Table *
new_table() {
    struct Table *table;

    table = malloc(sizeof(struct Table));
    if (table == NULL) {
        err("malloc: %s", strerror(errno));
        return NULL;
    }

    table->name = NULL;
    table->use_proxy_header = 0;
    table->reference_count = 0;
    STAILQ_INIT(&table->backends);

    return table;
}

int
accept_table_arg(struct Table *table, const char *arg) {
    if (table->name == NULL) {
        table->name = strdup(arg);
        if (table->name == NULL) {
            err("strdup: %s", strerror(errno));
            return -1;
        }
    } else {
        err("Unexpected table argument: %s", arg);
        return -1;
    }

    return 1;
}


void
add_table(struct Table_head *tables, struct Table *table) {
    table_ref_get(table);
    SLIST_INSERT_HEAD(tables, table, entries);
}

void init_table(struct Table *table) {
    struct Backend *iter;

    STAILQ_FOREACH(iter, &table->backends, entries)
        init_backend(iter);
}

void
free_tables(struct Table_head *tables) {
    struct Table *iter;

    while ((iter = SLIST_FIRST(tables)) != NULL) {
        SLIST_REMOVE_HEAD(tables, entries);
        table_ref_put(iter);
    }
}

struct Table *
table_lookup(const struct Table_head *tables, const char *name) {
    struct Table *iter;

    SLIST_FOREACH(iter, tables, entries) {
        if (iter->name == NULL && name == NULL) {
            return iter;
        } else if (iter->name != NULL && name != NULL &&
                strcmp(iter->name, name) == 0) {
            return iter;
        }
    }

    return NULL;
}

void
remove_table(struct Table_head *tables, struct Table *table) {
    SLIST_REMOVE(tables, table, Table, entries);
    table_ref_put(table);
}

const struct Address *
table_lookup_server_address(const struct Table *table, const char *name, size_t name_len) {
    struct Backend *b;

    b = table_lookup_backend(table, name, name_len);
    if (b == NULL) {
        info("No match found for %.*s", (int)name_len, name);
        return NULL;
    }

    return b->address;
}

void
reload_tables(struct Table_head *tables, struct Table_head *new_tables) {
    struct Table *iter;

    /* Remove unused tables which were removed from the new configuration */
    /* Unused elements at the beginning of the list */
    while ((iter = SLIST_FIRST(tables)) != NULL &&
            table_lookup(new_tables, SLIST_FIRST(tables)->name) == NULL) {
        SLIST_REMOVE_HEAD(tables, entries);
        table_ref_put(iter);
    }
    /* Remove elements following first used element */
    SLIST_FOREACH(iter, tables, entries) {
        if (SLIST_NEXT(iter, entries) != NULL &&
                table_lookup(new_tables,
                        SLIST_NEXT(iter, entries)->name) == NULL) {
            struct Table *temp = SLIST_NEXT(iter, entries);
            /* SLIST remove next */
            SLIST_NEXT(iter, entries) = SLIST_NEXT(temp, entries);
            table_ref_put(temp);
        }
    }


    while ((iter = SLIST_FIRST(new_tables)) != NULL) {
        SLIST_REMOVE_HEAD(new_tables, entries);

        /* Initialize table regular expressions */
        init_table(iter);

        struct Table *existing = table_lookup(tables, iter->name);
        if (existing) {
            /* Swap table contents */
            struct Backend_head temp = existing->backends;
            existing->backends = iter->backends;
            iter->backends = temp;
        } else {
            add_table(tables, iter);
        }
        table_ref_put(iter);
    }
}

void
print_table_config(FILE *file, struct Table *table) {
    struct Backend *backend;

    if (table->name == NULL)
        fprintf(file, "table {\n");
    else
        fprintf(file, "table %s {\n", table->name);

    STAILQ_FOREACH(backend, &table->backends, entries) {
        print_backend_config(file, backend);
    }
    fprintf(file, "}\n\n");
}

static void
free_table(struct Table *table) {
    struct Backend *iter;

    if (table == NULL)
        return;

    while ((iter = STAILQ_FIRST(&table->backends)) != NULL)
        remove_backend(&table->backends, iter);

    free(table->name);
    free(table);
}

void
table_ref_put(struct Table *table) {
    if (table == NULL)
        return;

    assert(table->reference_count > 0);
    table->reference_count--;
    if (table->reference_count == 0)
        free_table(table);
}

struct Table *
table_ref_get(struct Table *table) {
    table->reference_count++;
    return table;
}
