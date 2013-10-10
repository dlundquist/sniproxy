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
#ifndef TABLE_H
#define TABLE_H

#include <stdio.h>
#include <sys/queue.h>
#include "backend.h"

#define TABLE_NAME_LEN 20

SLIST_HEAD(Table_head, Table);

struct Table {
    char *name;

    /* Runtime fields */
    struct Backend_head backends;
    SLIST_ENTRY(Table) entries;
};

struct Table *new_table();
int accept_table_arg(struct Table *, char *);
void add_table(struct Table_head *, struct Table *);
struct Table *lookup_table(const struct Table_head *, const char *);
int lookup_table_server_socket(const struct Table *, const char *);
void print_table_config(FILE *, struct Table *);
int valid_table(struct Table *);
void free_table(struct Table *);
void init_table(struct Table *);


void free_tables(struct Table_head *);



static inline struct Backend *
lookup_table_backend(const struct Table *table, const char *hostname) {
    return lookup_backend(&table->backends, hostname);
}

static inline void
remove_table_backend(struct Table *table, struct Backend *backend) {
    remove_backend(&table->backends, backend);
}
#endif
