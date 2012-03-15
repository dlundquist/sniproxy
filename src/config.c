#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp() */
#include <assert.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include "cfg_parser.h"
#include "config.h"
#include "util.h"


struct ListenerConfig {
    struct Config *config;
    char *address;
    char *port;
    char *table_name;
    char *protocol;
};

struct TableConfig {
    struct Config *config;
    char *name;
    STAILQ_HEAD(, TableEntryConfig) entries;
};

struct TableEntryConfig {
    struct TableConfig *table;
    char *hostname;
    char *address;
    char *port;
    STAILQ_ENTRY(TableEntryConfig) entries;
};

static int accept_username(struct Config *, char *, size_t);

static struct ListenerConfig *begin_listener(struct Config *);
static int accept_listener_arg(struct ListenerConfig *, char *, size_t);
static int accept_listener_table_name(struct ListenerConfig *, char *, size_t);
static int accept_listener_protocol(struct ListenerConfig *, char *, size_t);
static int end_listener(struct ListenerConfig *);

static struct TableConfig *begin_table(struct Config *);
static int accept_table_arg(struct TableConfig *, char *, size_t);
static int end_table(struct TableConfig *);

static struct TableEntryConfig *begin_table_entry(struct TableConfig *);
static int accept_table_entry_arg(struct TableEntryConfig *, char *, size_t);
static int end_table_entry(struct TableEntryConfig *);

static struct Keyword listener_stanza_grammar[] = {
    { "protocol",
            NULL,
            (int(*)(void *, char *, size_t))accept_listener_protocol,
            NULL,
            NULL},
    { "table",
            NULL,
            (int(*)(void *, char *, size_t))accept_listener_table_name,
            NULL,
            NULL},
    { NULL, NULL, NULL, NULL, NULL }
};

static struct Keyword table_stanza_grammar[] = {
    { NULL,
            (void *(*)(void *))begin_table_entry,
            (int(*)(void *, char *, size_t))accept_table_entry_arg,
            NULL,
            (int(*)(void *))end_table_entry},
};

static struct Keyword global_grammar[] = {
    { "username",
            NULL,
            (int(*)(void *, char *, size_t))accept_username,
            NULL,
            NULL},
    { "listener",
            (void *(*)(void *))begin_listener,
            (int(*)(void *, char *, size_t))accept_listener_arg,
            listener_stanza_grammar,
            (int(*)(void *))end_listener},
    { "table",
            (void *(*)(void *))begin_table,
            (int(*)(void *, char *, size_t))accept_table_arg,
            table_stanza_grammar,
            (int(*)(void *))end_table},
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
    
    parse_config((void *)config, file, global_grammar);

    fclose(file);

    return(config);
}

void
free_config(struct Config *c) {
    assert(c != NULL);

    free(c->filename);
    /* TODO free nested objects */

    free(c);
}


int
reload_config(struct Config *c) {
/*
    FILE *config;
    int done = 0;
    enum Token token;
    char buffer[BUFFER_SIZE];

    assert(c != NULL);
    assert(c->filename != NULL);

    config = fopen(c->filename, "r");
    if (config == NULL) {
        fprintf(stderr, "Unable to open %s\n", c->filename);
        return -1;
    }

    while(done == 0) {
        token = next_token(config, buffer, sizeof(buffer));
        switch (token) {
            case SEPERATOR:
*/
                /* no op */
/*
                break;
            case USER:
                if (c->user != NULL)
                    fprintf(stderr, "Warning: user already specified\n");
               
                token = next_token(config, buffer, sizeof(buffer));
                if (token != WORD) {
                    fprintf(stderr, "Error parsing config near %s\n", buffer);
                    return -1;
                }

                c->user = strdup(buffer);
                if (c->user == NULL) {
                    perror("malloc()");
                    return -1;
                }
                break;
            case LISTEN:
                parse_listen_stanza(c, config);
                break;
            case TABLE:
                parse_table_stanza(c, config);
                break;
            case ENDCONFIG:
                done = 1;
                break;
            default:
                fprintf(stderr, "Error parsing config near %s\n", buffer);
                return -1;
        }
    }
    fclose(config);

*/
    if (c == NULL)
        return 1;
    /* TODO validate config */
    return 0;
}

static void print_listener_config(struct Listener *);
static void print_table_config(struct Table *);

void print_config(struct Config *config) {
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

static void
print_listener_config(struct Listener *listener) {
    char addr_str[INET_ADDRSTRLEN];
    union {
        struct sockaddr_storage *storage;
        struct sockaddr_in *sin;
        struct sockaddr_in6 *sin6;
        struct sockaddr_un *sun;
    } addr;
    
    addr.storage = &listener->addr;

    if (addr.storage->ss_family == AF_UNIX) {
        printf("listener unix:%s {\n", addr.sun->sun_path);
    } else if (addr.storage->ss_family == AF_INET) {
        inet_ntop(AF_INET, &addr.sin->sin_addr, addr_str, listener->addr_len);
        printf("listener %s %d {\n", addr_str, ntohs(addr.sin->sin_port));
    } else {
        inet_ntop(AF_INET6, &addr.sin6->sin6_addr, addr_str, listener->addr_len);
        printf("listener %s %d {\n", addr_str, ntohs(addr.sin6->sin6_port));
    }

    if (listener->protocol == TLS)
        printf("\tprotocol tls\n");
    else
        printf("\tprotocol http\n");

    if (listener->table_name)
        printf("\ttable %s\n", listener->table_name);


    printf("}\n\n");
}

static void
print_table_config(struct Table *table) {
    struct Backend *backend;

    if (table->name == NULL)
        printf("table {\n");
    else
        printf("table %s {\n", table->name);

    STAILQ_FOREACH(backend, &table->backends, entries) {
        if (backend->port == 0)
            printf("\t%s %s\n", backend->hostname, backend->address);
        else 
            printf("\t%s %s %d\n", backend->hostname, backend->address, backend->port);
    }
    printf("}\n\n");
}

static int
accept_username(struct Config *config, char *username, size_t len) {
        config->user = strndup(username, len);
        return 1;
}

static struct ListenerConfig *
begin_listener(struct Config *config) {
    struct ListenerConfig *listener;

    listener = calloc(1, sizeof(struct ListenerConfig));
    if (listener == NULL) {
        perror("calloc");
        return NULL;
    }

    listener->config = config;
    listener->address = NULL;
    listener->port = NULL;
    listener->table_name = NULL;
    listener->protocol = NULL;

    return listener;
}

static int
accept_listener_arg(struct ListenerConfig *listener, char *arg, size_t len) {
    if (listener->address == NULL)
        if (isnumeric(arg))
            listener->port = strndup(arg, len);
        else
            listener->address = strndup(arg, len);
    else if (listener->port == NULL && isnumeric(arg))
        listener->port = strndup(arg, len);
    else
        return -1;

    return 1;
}

static int
accept_listener_table_name(struct ListenerConfig *listener, char *table_name, size_t len) {
    if (listener->table_name == NULL)
        listener->table_name = strndup(table_name, len);
    else
        fprintf(stderr, "Duplicate table_name: %s\n", table_name);

    return 1;
}

static int
accept_listener_protocol(struct ListenerConfig *listener, char *protocol, size_t len) {
    if (listener->protocol == NULL)
        listener->protocol = strndup(protocol, len);
    else
        fprintf(stderr, "Duplicate protocol: %s\n", protocol);
            
    return 1;
}

static int
end_listener(struct ListenerConfig *lc) {
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

    SLIST_INSERT_HEAD(&lc->config->listeners, listener, entries);

    return 1;
}

static struct TableConfig *
begin_table(struct Config *config) {
    struct TableConfig *table;

    table = calloc(1, sizeof(struct TableConfig));
    if (table == NULL) {
        perror("calloc");
        return NULL;
    }

    table->config = config;
    STAILQ_INIT(&table->entries);

    return table;
}

static int
accept_table_arg(struct TableConfig *table, char *arg, size_t len) {
    if (table->name == NULL)
        table->name = strndup(arg, len);
    else
        fprintf(stderr, "Unexpected table argument: %s\n", arg);

    return 1;
}

static int
end_table(struct TableConfig *tc) {
    struct Table *table;
    struct TableEntryConfig *entry;
    int port;

    table = malloc(sizeof(struct Table));
    STAILQ_INIT(&table->backends);
    
    if (table == NULL) {
        perror("malloc");
        return -1;
    }
    table->name = tc->name;
    tc->name = NULL;
    
    STAILQ_FOREACH(entry, &tc->entries, entries) {
        port = 0;
        if (entry->port != NULL)
            port = atoi(entry->port);

        add_backend(&table->backends, entry->hostname, entry->address, port);
    }

    SLIST_INSERT_HEAD(&tc->config->tables, table, entries);

    return 1;
}

static struct TableEntryConfig *
begin_table_entry(struct TableConfig *table) {
    struct TableEntryConfig *entry;

    entry = calloc(1, sizeof(struct TableEntryConfig));
    if (entry == NULL) {
        perror("malloc");
        return NULL;
    }

    entry->table = table;

    return entry;
}

static int
accept_table_entry_arg(struct TableEntryConfig *entry, char *arg, size_t len) {
    if (entry->hostname == NULL)
        entry->hostname = strndup(arg, len);
    else if (entry->address == NULL)
        entry->address = strndup(arg, len);
    else if (entry->port == NULL)
        entry->port = strndup(arg, len);
    else
        fprintf(stderr, "Unexpected table entry argument: %s\n", arg);

    return 1;
}

static int
end_table_entry(struct TableEntryConfig *entry) {
    STAILQ_INSERT_TAIL(&entry->table->entries, entry, entries);
    return 1;
}
