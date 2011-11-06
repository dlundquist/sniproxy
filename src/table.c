#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <syslog.h>
#include "table.h"
#include "backend.h"


static SLIST_HEAD(, Table) tables;


static void free_table(struct Table *);


void init_tables() {
    SLIST_INIT(&tables);
}

void free_tables() {
    struct Table *iter;

    while ((iter = SLIST_FIRST(&tables)) != NULL) {
        SLIST_REMOVE_HEAD(&tables, entries);
        free_table(iter);
    }
}

struct Table *add_table(const char *name) {
    struct Table *table;

    table = calloc(1, sizeof(struct Table));
    if (table == NULL) {
        syslog(LOG_CRIT, "calloc failed");
        return NULL;
    }
    if (strlen(name) >= sizeof(table->name)) {
        syslog(LOG_CRIT, "table name \"%s\" too long", name);
        free(table);
        return NULL;
    }
    strncpy(table->name, name, sizeof(table->name));
    STAILQ_INIT(&table->backends);

    SLIST_INSERT_HEAD(&tables, table, entries);
   
    return table;
}

struct Table *lookup_table(const char *name) {
    struct Table *iter;

    SLIST_FOREACH(iter, &tables, entries) {
        if (strcmp(iter->name, name) == 0)
            return iter;
    }

    return NULL;
}

void remove_table(struct Table *table) {
    SLIST_REMOVE(&tables, table, Table, entries);
    free_table(table);
}

struct Backend *
add_table_backend(struct Table *table, const char *hostname, const char *address, int port) {
    struct Backend *b;
    const char *reerr;
    int reerroffset;
    int i;

    b = calloc(1, sizeof(struct Backend));
    if (b == NULL) {
        syslog(LOG_CRIT, "calloc failed");
        return NULL;
    }

    strncpy(b->hostname, hostname, HOSTNAME_REGEX_LEN - 1);

    b->hostname_re = pcre_compile(hostname, 0, &reerr, &reerroffset, NULL);
    if (b->hostname_re == NULL) {
        syslog(LOG_CRIT, "Regex compilation failed: %s, offset %d", reerr, reerroffset);
        free(b);
        return NULL;
    }

    for (i = 0; i < BACKEND_ADDRESS_LEN && address[i] != '\0'; i++)
        b->address[i] = tolower(address[i]);

    b->port = port;

    syslog(LOG_DEBUG, "Parsed %s %s %d", hostname, address, port);
    STAILQ_INSERT_TAIL(&table->backends, b, entries);

    return b;
}

struct Backend *
lookup_table_backend(const struct Table *table, const char *hostname) {
    struct Backend *iter;

    if (hostname == NULL)
        hostname = "";

    STAILQ_FOREACH(iter, &table->backends, entries) {
        if (pcre_exec(iter->hostname_re, NULL, hostname, strlen(hostname), 0, 0, NULL, 0) >= 0) {
            syslog(LOG_DEBUG, "%s matched %s", iter->hostname, hostname);
            return iter;
        } else {
            syslog(LOG_DEBUG, "%s didn't match %s", iter->hostname, hostname);
        }
    }
    return NULL;
}

void
remove_table_backend(struct Table *table, struct Backend *backend) {
    STAILQ_REMOVE(&table->backends, backend, Backend, entries);
    free(backend);
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

    while ((iter = STAILQ_FIRST(&table->backends)) != NULL) {
        STAILQ_REMOVE_HEAD(&table->backends, entries);
        free(iter);
    }
}
