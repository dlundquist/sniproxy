#ifndef TABLE_H
#define TABLE_H

#include <sys/queue.h>

struct Table {
    const char *name;

    /* Runtime fields */
    STAILQ_HEAD(, Backend) backends;
    SLIST_ENTRY(Table) entries;
};

int lookup_table_server_socket(const struct Table *, const char *);
#endif
