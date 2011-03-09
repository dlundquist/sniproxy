#ifndef CONFIG_H
#define CONFIG_H

#include <sys/queue.h>
#include <netinet/in.h>
#include "backend.h"
#include "listener.h"

struct Config {
    char *filename;
    char *user;
    LIST_HEAD(, Table) tables; /* TODO use a hash */
    LIST_HEAD(, Listener) listeners;
};

struct Config *init_config(const char *);
int reload_config(struct Config *);
void free_config(struct Config *);

#endif
