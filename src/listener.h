#ifndef LISTENER_H
#define LISTENER_H
#include <sys/select.h>
#include <netinet/in.h>
#include "table.h"

SLIST_HEAD(Listener_head, Listener);

struct Listener {
    /* Configuration fields */
    struct sockaddr_storage addr;
    size_t addr_len;
    enum Protocol {
        TLS,
        HTTP
    } protocol;
    char *table_name;

    /* Runtime fields */
    int sockfd;
    const char *(*parse_packet)(const char *, int);
    void (*close_client_socket)(int);
    struct Table *table;
    SLIST_ENTRY(Listener) entries;
};

struct Listener *new_listener();
int accept_listener_arg(struct Listener *, char *);
int accept_listener_table_name(struct Listener *, char *);
int accept_listener_protocol(struct Listener *, char *);

void add_listener(struct Listener_head *, struct Listener *);
int init_listeners(struct Listener_head *, const struct Table_head *);
int fd_set_listeners(const struct Listener_head *, fd_set *, int);
void handle_listeners(struct Listener_head *, fd_set *);
void remove_listener(struct Listener_head *, struct Listener *);
void free_listeners(struct Listener_head *);

int valid_listener(const struct Listener *);
void print_listener_config(FILE *, const struct Listener *);
void print_listener_status(FILE *, const struct Listener *);
int init_listener(struct Listener *, const struct Table_head *);
void free_listener(struct Listener *);

static inline int lookup_server_socket(const struct Listener *listener, const char *hostname) {
    return lookup_table_server_socket(listener->table, hostname);
}
#endif
