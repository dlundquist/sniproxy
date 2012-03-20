#include <stdio.h>
#include <string.h>
#include "cfg_parser.h"
#include "config.h"


static int accept_username(struct Config *, char *);
static int end_listener_stanza(struct Config *, struct Listener *);
static int end_table_stanza(struct Config *, struct Table *);
static int end_backend(struct Table *, struct Backend *);

struct Keyword listener_stanza_grammar[] = {
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
            (void *(*)())new_backend,
            (int(*)(void *, char *))accept_backend_arg,
            NULL,
            (int(*)(void *, void *))end_backend},
};

static struct Keyword global_grammar[] = {
    { "username",
            NULL,
            (int(*)(void *, char *))accept_username,
            NULL,
            NULL},
    { "listener",
            (void *(*)())new_listener,
            (int(*)(void *, char *))accept_listener_arg,
            listener_stanza_grammar,
            (int(*)(void *, void *))end_listener_stanza},
    { "table",
            (void *(*)())new_table,
            (int(*)(void *, char *))accept_table_arg,
            table_stanza_grammar,
            (int(*)(void *, void *))end_table_stanza},
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
        long whence = ftell(file);
        char buffer[256];

        fprintf(stderr, "error parsing %s at %ld near: %s\n", filename, whence);
        fseek(file, -20, SEEK_CUR);
        for (int i = 0; i < 5; i++) {
            fprintf(stderr, "%d\t%s", ftell(file), fgets(buffer, sizeof(buffer), file));
        }

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
print_config(FILE *file, struct Config *config) {
    struct Listener *listener = NULL;
    struct Table *table = NULL;

    if (config->filename)
        fprintf(file, "# Config loaded from %s\n\n", config->filename);

    if (config->user)
        fprintf(file, "username %s\n\n", config->user);

    SLIST_FOREACH(listener, &config->listeners, entries) {
        print_listener_config(file, listener);
    }

    SLIST_FOREACH(table, &config->tables, entries) {
        print_table_config(file, table);
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


static int
end_listener_stanza(struct Config *config, struct Listener *listener) {
    if (valid_listener(listener) <= 0) {
        fprintf(stderr, "Invalid listener\n");
        print_listener_config(stderr, listener);
        free_listener(listener);
        return -1;
    }

    add_listener(&config->listeners, listener);

    return 1;
}

static int
end_table_stanza(struct Config *config, struct Table *table) {
    /* TODO check table */

    add_table(&config->tables, table);
   
    return 1;
}

static int
end_backend(struct Table *table, struct Backend *backend) {
    /* TODO check backend */

    add_backend(&table->backends, backend);
    return 1;
}

