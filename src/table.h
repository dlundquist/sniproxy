#ifndef TABLE_H
#define TABLE_H

#include <sys/queue.h>

struct Table {
    char char * name;

    /* Runtime fields */
    STAILQ_HEAD(, Backend) backends;
    SLIST_ENTRY(Table) entries;
};

#endif
