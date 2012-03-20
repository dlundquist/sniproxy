#ifndef CONFIG_H
#define CONFIG_H

#include <sys/queue.h>
#include <netinet/in.h>
#include "table.h"
#include "listener.h"

struct Config {
    char *filename;
    char *user;
    struct Listener_head listeners;
    struct Table_head tables;
};

struct Config *init_config(const char *);
int reload_config(struct Config *);
void free_config(struct Config *);
void print_config(FILE *, struct Config *);

#endif
