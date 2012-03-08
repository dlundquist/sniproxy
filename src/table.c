#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <syslog.h>
#include "table.h"
#include "backend.h"


static SLIST_HEAD(, Table) tables;


static void free_table(struct Table *);


void
init_tables() {
    SLIST_INIT(&tables);
}

void
free_tables() {
    struct Table *iter;

    while ((iter = SLIST_FIRST(&tables)) != NULL) {
        SLIST_REMOVE_HEAD(&tables, entries);
        free_table(iter);
    }
}

struct Table *
add_table(const char *name) {
    struct Table *table;

    table = calloc(1, sizeof(struct Table));
    if (table == NULL) {
        syslog(LOG_CRIT, "calloc failed");
        return NULL;
    }
    table->name = strdup(name);
    if (table->name == NULL) {
        syslog(LOG_CRIT, "strdup failed");
        free(table);
        return NULL;
    }
    STAILQ_INIT(&table->backends);

    SLIST_INSERT_HEAD(&tables, table, entries);
   
    return table;
}

struct Table *
lookup_table(const char *name) {
    struct Table *iter;

    SLIST_FOREACH(iter, &tables, entries) {
        if (strcmp(iter->name, name) == 0)
            return iter;
    }

    return NULL;
}

void
remove_table(struct Table *table) {
    SLIST_REMOVE(&tables, table, Table, entries);
    free_table(table);
}

int
lookup_table_server_socket(const struct Table *table, const char *hostname) {
    struct Backend *b;

    b = lookup_table_backend(table, hostname);
    if (b == NULL) {
        syslog(LOG_INFO, "No match found for %s", hostname);
        return -1;
    }

    return open_backend_socket(b, hostname);
}

static void
free_table(struct Table *table) {
    struct Backend *iter;

    while ((iter = STAILQ_FIRST(&table->backends)) != NULL)
        remove_backend(&table->backends, iter);
    free(table->name);
    free(table);
}
