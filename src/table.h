#ifndef TABLE_H
#define TABLE_H

#include <sys/queue.h>
#include "backend.h"

#define TABLE_NAME_LEN 20

struct Table {
    char *name;

    /* Runtime fields */
    struct Backend_head backends;
    SLIST_ENTRY(Table) entries;
};

void init_tables();
void free_tables();
struct Table *add_table(const char *);
struct Table *lookup_table(const char *);
void remove_table(struct Table *);
int lookup_table_server_socket(const struct Table *, const char *);

static inline struct Backend *
add_table_backend(struct Table *table, const char *hostname, const char *address, int port) {
    return add_backend(&table->backends, hostname, address, port);
}

static inline struct Backend *
lookup_table_backend(const struct Table *table, const char *hostname) {
    return lookup_backend(&table->backends, hostname);
}

static inline void
remove_table_backend(struct Table *table, struct Backend *backend) {
    remove_backend(&table->backends, backend);
}
#endif
