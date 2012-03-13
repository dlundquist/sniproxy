#ifndef CONFIG_H
#define CONFIG_H

#include <sys/queue.h>
#include <netinet/in.h>
#include "table.h"
#include "listener.h"

struct Config {
    char *filename;
    char *user;
    SLIST_HEAD(, Listener) listeners;
    SLIST_HEAD(, Table) tables; /* TODO use a hash */
};

struct Config *init_config(const char *);
int reload_config(struct Config *);
void free_config(struct Config *);
void print_config(struct Config *);

#endif
