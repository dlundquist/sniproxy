#ifndef TABLE_H
#define TABLE_H

#include <sys/queue.h>

#define TABLE_NAME_LEN 20

struct Table {
    char name[TABLE_NAME_LEN];

    /* Runtime fields */
    STAILQ_HEAD(, Backend) backends;
    SLIST_ENTRY(Table) entries;
};

void init_tables();
void free_tables();
struct Table *add_table(const char *);
struct Table *lookup_table(const char *);
void remove_table(struct Table *);
struct Backend *add_table_backend(struct Table *, const char *, const char *, int);
struct Backend *lookup_table_backend(const struct Table *, const char *);
void remove_table_backend(struct Table *, struct Backend *);
int lookup_table_server_socket(const struct Table *, const char *);
#endif
