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


static int accept_username(struct Config *, char *);

static struct Listener *begin_listener();
static int accept_listener_arg(struct Listener *, char *);
static int accept_listener_table_name(struct Listener *, char *);
static int accept_listener_protocol(struct Listener *, char *);
static int end_listener(struct Config *, struct Listener *);

static struct Table *begin_table();
static int accept_table_arg(struct Table *, char *);
static int end_table(struct Config *, struct Table *);

static struct Backend *begin_table_entry();
static int accept_table_entry_arg(struct Backend *, char *);
static int end_table_entry(struct Table *, struct Backend *);

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

static struct Listener *
begin_listener() {
    struct Listener *listener;

    listener = calloc(1, sizeof(struct Listener));
    if (listener == NULL) {
        perror("malloc");
        return NULL;
    }

    listener->protocol = TLS;

    return listener;
}

static int
accept_listener_arg(struct Listener *listener, char *arg) {
    if (listener->addr.ss_family == 0) {
        if (isnumeric(arg))
            listener->addr_len = parse_address(&listener->addr, "::", atoi(arg));
        else 
            listener->addr_len = parse_address(&listener->addr, arg, 0);

        if (listener->addr_len == 0) {
            fprintf(stderr, "Invalid listener argument %s\n", arg);
            return -1;
        }
    } else if (listener->addr.ss_family == AF_INET && isnumeric(arg)) {
        ((struct sockaddr_in *)&listener->addr)->sin_port = htons(atoi(arg));
    } else if (listener->addr.ss_family == AF_INET6 && isnumeric(arg)) {
        ((struct sockaddr_in6 *)&listener->addr)->sin6_port = htons(atoi(arg));
    } else {
        fprintf(stderr, "Invalid listener argument %s\n", arg);
        return -1;
    }
    

    return 1;
}

static int
accept_listener_table_name(struct Listener *listener, char *table_name) {
    if (listener->table_name == NULL)
        listener->table_name = strdup(table_name);
    else
        fprintf(stderr, "Duplicate table_name: %s\n", table_name);

    return 1;
}

static int
accept_listener_protocol(struct Listener *listener, char *protocol) {
    if (listener->protocol == 0 && strcasecmp(protocol, "http") == 0)
        listener->protocol = HTTP;
    else
        listener->protocol = TLS;

    if (listener->addr.ss_family == AF_INET && ((struct sockaddr_in *)&listener->addr)->sin_port == 0)
        ((struct sockaddr_in *)&listener->addr)->sin_port = listener->protocol == TLS ? 443 : 80;
    else if (listener->addr.ss_family == AF_INET6 && ((struct sockaddr_in6 *)&listener->addr)->sin6_port == 0)
        ((struct sockaddr_in6 *)&listener->addr)->sin6_port = listener->protocol == TLS ? 443 : 80;
            
    return 1;
}

static int
end_listener(struct Config *config, struct Listener *listener) {

    SLIST_INSERT_HEAD(&config->listeners, listener, entries);

    return 1;
}



static struct Table *
begin_table() {
    struct Table *table;

    table = malloc(sizeof(struct Table));
    if (table == NULL) {
        perror("malloc");
        return NULL;
    }

    table->name = NULL;
    STAILQ_INIT(&table->backends);

    return table;
}

static int
accept_table_arg(struct Table *table, char *arg) {
    if (table->name == NULL) {
        table->name = strdup(arg);
        if (table->name == NULL) {
            perror("strdup");
            return -1;
        }
    } else {
        fprintf(stderr, "Unexpected table argument: %s\n", arg);
        return -1;
    }

    return 1;
}

static int
end_table(struct Config *config, struct Table *table) {
    /* TODO check table */

    SLIST_INSERT_HEAD(&config->tables, table, entries);
   
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
end_table_entry(struct Table *table, struct Backend *entry) {
    const char *reerr; 
    int reerroffset;

    entry->hostname_re = pcre_compile(entry->hostname, 0, &reerr, &reerroffset, NULL);
    if (entry->hostname_re == NULL) {
        fprintf(stderr, "Regex compilation failed: %s, offset %d", reerr, reerroffset);
        return -1;
    }

    STAILQ_INSERT_TAIL(&table->backends, entry, entries);
    return 1;
}
