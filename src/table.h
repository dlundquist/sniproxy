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
#include "address.h"

#define TABLE_NAME_LEN 20

SLIST_HEAD(Table_head, Table);

struct Table {
    char *name;
    int use_proxy_header;

    /* Runtime fields */
    int reference_count;
    struct Backend_head backends;
    SLIST_ENTRY(Table) entries;
};

struct Table *new_table();
int accept_table_arg(struct Table *, const char *);
void add_table(struct Table_head *, struct Table *);
struct Table *table_lookup(const struct Table_head *, const char *);
const struct Address *table_lookup_server_address(const struct Table *,
                                                  const char *, size_t);
void reload_tables(struct Table_head *, struct Table_head *);
void print_table_config(FILE *, struct Table *);
int valid_table(struct Table *);
void init_table(struct Table *);
void table_ref_put(struct Table *);
struct Table *table_ref_get(struct Table *);
void tables_reload(struct Table_head *, struct Table_head *);

void free_tables(struct Table_head *);

#endif
