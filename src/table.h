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
void print_table_status(FILE *, struct Table *);
void remove_table(struct Table_head *, struct Table *);
void free_table(struct Table *);


void init_tables(struct Table_head *);
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
