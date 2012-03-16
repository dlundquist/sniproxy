#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp() */
#include <assert.h>
#include <ctype.h> /* tolower */      
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include "cfg_parser.h"
#include "config.h"
#include "util.h"


struct ListenerConfig {
    char *address;
    char *port;
    char *table_name;
    char *protocol;
};

struct TableConfig {
    char *name;
    STAILQ_HEAD(, Backend) entries;
};

static int accept_username(struct Config *, char *);

static struct ListenerConfig *begin_listener();
static int accept_listener_arg(struct ListenerConfig *, char *);
static int accept_listener_table_name(struct ListenerConfig *, char *);
static int accept_listener_protocol(struct ListenerConfig *, char *);
static int end_listener(struct Config *, struct ListenerConfig *);

static struct TableConfig *begin_table();
static int accept_table_arg(struct TableConfig *, char *);
static int end_table(struct Config *, struct TableConfig *);

static struct Backend *begin_table_entry();
static int accept_table_entry_arg(struct Backend *, char *);
static int end_table_entry(struct TableConfig *, struct Backend *);

static struct Keyword listener_stanza_grammar[] = {
    { "protocol",
            NULL,
            (int(*)(void *, char *))accept_listener_protocol,
            NULL,
            NULL},
    { "table",
            NULL,
            (int(*)(void *, char *))accept_listener_table_name,
            NULL,
            NULL},
    { NULL, NULL, NULL, NULL, NULL }
};

static struct Keyword table_stanza_grammar[] = {
    { NULL,
            (void *(*)())begin_table_entry,
            (int(*)(void *, char *))accept_table_entry_arg,
            NULL,
            (int(*)(void *, void *))end_table_entry},
};

static struct Keyword global_grammar[] = {
    { "username",
            NULL,
            (int(*)(void *, char *))accept_username,
            NULL,
            NULL},
    { "listener",
            (void *(*)())begin_listener,
            (int(*)(void *, char *))accept_listener_arg,
            listener_stanza_grammar,
            (int(*)(void *, void *))end_listener},
    { "table",
            (void *(*)())begin_table,
            (int(*)(void *, char *))accept_table_arg,
            table_stanza_grammar,
            (int(*)(void *, void *))end_table},
    { NULL, NULL, NULL, NULL, NULL }
};

struct Config *
init_config(const char *filename) {
    FILE *file;
    struct Config *config;

    config = malloc(sizeof(struct Config));
    if (config == NULL) {
        perror("malloc()");
        return NULL;
    }

    config->filename = NULL;
    config->user = NULL;
    SLIST_INIT(&config->listeners);
    SLIST_INIT(&config->tables);

    config->filename = strdup(filename);
    if (config->filename == NULL) {
        perror("malloc()");
        free_config(config);
        return NULL;
    }


    file = fopen(config->filename, "r");
    
    if (parse_config((void *)config, file, global_grammar) <= 0) {
        fprintf(stderr, "error parsing config\n");
        free_config(config);
        config = NULL;
    }

    fclose(file);


    return(config);
}

void
free_config(struct Config *config) {
    struct Listener *listener;
    struct Table *table;

    if (config->filename)
        free(config->filename);
    if (config->user)
        free(config->user);

    while ((listener = SLIST_FIRST(&config->listeners)) != NULL) {
        SLIST_REMOVE_HEAD(&config->listeners, entries);
        free_listener(listener);
    }

    while ((table = SLIST_FIRST(&config->tables)) != NULL) {
        SLIST_REMOVE_HEAD(&config->tables, entries);
        free_table(table);
    }

    free(config);
}

int
reload_config(struct Config *config) {
    if (config == NULL)
        return 1;
    /* TODO validate config */
    return 0;
}

void
print_config(struct Config *config) {
    struct Listener *listener = NULL;
    struct Table *table = NULL;

    printf("# Config loaded from %s\n\n", config->filename);

    if (config->user)
        printf("username %s\n\n", config->user);

    SLIST_FOREACH(listener, &config->listeners, entries) {
        print_listener_config(listener);
    }

    SLIST_FOREACH(table, &config->tables, entries) {
        print_table_config(table);
    }
}



static int
accept_username(struct Config *config, char *username) {
        config->user = strdup(username);
        if (config->user == NULL) {
            perror("malloc:");
            return -1;
        }
        return 1;
}



static struct ListenerConfig *
begin_listener() {
    struct ListenerConfig *listener;

    listener = malloc(sizeof(struct ListenerConfig));
    if (listener == NULL) {
        perror("malloc");
        return NULL;
    }

    listener->address = NULL;
    listener->port = NULL;
    listener->table_name = NULL;
    listener->protocol = NULL;

    return listener;
}

static int
accept_listener_arg(struct ListenerConfig *listener, char *arg) {
    if (listener->address == NULL)
        if (isnumeric(arg))
            listener->port = strdup(arg);
        else
            listener->address = strdup(arg);
    else if (listener->port == NULL && isnumeric(arg))
        listener->port = strdup(arg);
    else
        return -1;

    return 1;
}

static int
accept_listener_table_name(struct ListenerConfig *listener, char *table_name) {
    if (listener->table_name == NULL)
        listener->table_name = strdup(table_name);
    else
        fprintf(stderr, "Duplicate table_name: %s\n", table_name);

    return 1;
}

static int
accept_listener_protocol(struct ListenerConfig *listener, char *protocol) {
    if (listener->protocol == NULL)
        listener->protocol = strdup(protocol);
    else
        fprintf(stderr, "Duplicate protocol: %s\n", protocol);
            
    return 1;
}

static int
end_listener(struct Config *config, struct ListenerConfig *lc) {
    struct Listener *listener;
    int port = 0;

    listener = malloc(sizeof(struct Listener));

    listener->table_name = lc->table_name;
    lc->table_name = NULL;

    listener->protocol = TLS;
    if (lc->protocol != NULL && strcasecmp(lc->protocol, "http") == 0)
        listener->protocol = HTTP;

    if (lc->port)
        port = atoi(lc->port);

    listener->addr_len = parse_address(&listener->addr, lc->address, port);

    SLIST_INSERT_HEAD(&config->listeners, listener, entries);

    if (lc->address)
        free(lc->address);
    if (lc->port)
        free(lc->port);
    if (lc->table_name)
        free(lc->table_name);
    if (lc->protocol)
        free(lc->protocol);
    free(lc);

    return 1;
}



static struct TableConfig *
begin_table() {
    struct TableConfig *table;

    table = malloc(sizeof(struct TableConfig));
    if (table == NULL) {
        perror("malloc");
        return NULL;
    }

    table->name = NULL;
    STAILQ_INIT(&table->entries);

    return table;
}

static int
accept_table_arg(struct TableConfig *table, char *arg) {
    if (table->name == NULL)
        table->name = strdup(arg);
    else
        fprintf(stderr, "Unexpected table argument: %s\n", arg);

    return 1;
}

static int
end_table(struct Config * config, struct TableConfig *tc) {
    struct Table *table;
    struct Backend *entry;

    table = malloc(sizeof(struct Table));
    if (table == NULL) {
        perror("malloc");
        return -1;
    }

    STAILQ_INIT(&table->backends);
    table->name = tc->name;
    tc->name = NULL;
    
    while ((entry = STAILQ_FIRST(&tc->entries)) != NULL)  {
        STAILQ_REMOVE_HEAD(&tc->entries, entries);
        STAILQ_INSERT_TAIL(&table->backends, entry, entries);
    }

    SLIST_INSERT_HEAD(&config->tables, table, entries);
   
    free(tc);

    return 1;
}



static struct Backend *
begin_table_entry() {
    struct Backend *entry;

    entry = calloc(1, sizeof(struct Backend));
    if (entry == NULL) {
        perror("malloc");
        return NULL;
    }

    return entry;
}

static int
accept_table_entry_arg(struct Backend *entry, char *arg) {
    char *ch;

    if (entry->hostname == NULL) {
        entry->hostname = strdup(arg);
        if (entry->hostname == NULL) {
            fprintf(stderr, "strdup failed");
            return -1;
        }
    } else if (entry->address == NULL) {
        entry->address = strdup(arg);
        if (entry->address == NULL) {
            fprintf(stderr, "strdup failed");
            return -1;
        }

        /* Store address as lower case */
        for (ch = entry->address; *ch == '\0'; ch++)
            *ch = tolower(*ch);
    } else if (entry->port == 0 && isnumeric(arg)) {
        entry->port = atoi(arg);
    } else {
        fprintf(stderr, "Unexpected table entry argument: %s\n", arg);
        return -1;
    }

    return 1;
}

static int
end_table_entry(struct TableConfig *table, struct Backend *entry) {
    const char *reerr; 
    int reerroffset;

    entry->hostname_re = pcre_compile(entry->hostname, 0, &reerr, &reerroffset, NULL);
    if (entry->hostname_re == NULL) {
        fprintf(stderr, "Regex compilation failed: %s, offset %d", reerr, reerroffset);
        return -1;
    }

    STAILQ_INSERT_TAIL(&table->entries, entry, entries);
    return 1;
}
