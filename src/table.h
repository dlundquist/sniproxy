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

#include <sys/queue.h>


SLIST_HEAD(Table_head, Table);

struct Table {
    char *name;

    /* Runtime fields */
    int reference_count;
    struct Backend *backends;
    size_t backend_count;
    SLIST_ENTRY(Table) entries;
};

struct Backend {
    char *pattern;
    int is_wildcard;
    char *rev_pattern;
    struct Address *address;
};


struct Table *new_table();
int accept_table_arg(struct Table *, const char *);
struct Table *table_lookup(const struct Table_head *, const char *);
const struct Address *table_lookup_server_address(const struct Table *,
                                                  const char *, size_t);
void reload_tables(struct Table_head *, struct Table_head *);
void print_table_config(FILE *, const struct Table *);
void table_ref_put(struct Table *);
struct Table *table_ref_get(struct Table *);
void free_table(struct Table *);

#endif
