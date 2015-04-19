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
#include "address.h"
#include "logger.h"


static char *strrev(char *);
static char *strnduprev(const char *, size_t);
static int backend_compare(const struct Backend *, const char *);


struct Table *
new_table() {
    struct Table *table;

    table = malloc(sizeof(struct Table));
    if (table == NULL) {
        err("malloc: %s", strerror(errno));
        return NULL;
    }

    table->name = NULL;
    table->reference_count = 0;

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

/*
 * Find a table by name
 */
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

/*
 * Find address by hostname
 */
const struct Address *
table_lookup_server_address(const struct Table *table, const char *name, size_t name_len) {
    /*
     * Binary search of backends by reserved hostname for prefix globbing
     */
    char *rev_name = strnduprev(name, name_len);

    while (strcmp(rev_name, "") != 0) {
        info("table_lookup_server_address: rev_name = %s", rev_name)





        char *dot = strrchr(rev_name, '.');
        if (dot) {
            strcpy(dot, ".*");
        } else {
            strcpy(rev_name, "*");
        }
    }


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

        struct Table *existing = table_lookup(tables, iter->name);
        if (existing) {
            /* Swap table contents */
            // TODO
        } else {
            table_ref_get(iter);
            SLIST_INSERT_HEAD(tables, iter, entries);
        }
        table_ref_put(iter);
    }
}

void
print_table_config(FILE *file, const struct Table *table) {
    struct Backend *backend;

    if (table->name == NULL)
        fprintf(file, "table {\n");
    else
        fprintf(file, "table %s {\n", table->name);

    fprintf(file, "}\n\n");
}

void
free_table(struct Table *table) {
    struct Backend *iter;

    if (table == NULL)
        return;

    // TODO free backends

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

/*
 * Reverse a string
 */
static char *
strrev(char *s) {
    size_t len = strlen(s);

    for (int i = 0; i < len / 2; i++) {
        char tmp = s[i];
        s[i] = s[len - i - 1];
        s[len - i - 1] = tmp;
    }

    return s;
}

static char *
strnduprev(const char *s, size_t n) {
    char *result = strndup(s, n);

    if (result != NULL)
        strrev(result);

    return result;
}

static int
backend_compare(const struct Backend *backend, const char *rev_name) {
    assert(backend);
    assert(backend->rev_pattern);
    assert(rev_name);

    if (backend->is_wildcard) {
        size_t pattern_len = strlen(backend->rev_pattern);
        return strncasecmp(backend->rev_pattern, rev_name, pattern_len);
    } else {
        return strcasecmp(backend->rev_pattern, rev_name);
    }
}
