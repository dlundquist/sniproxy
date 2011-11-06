#ifndef LISTENER_H
#define LISTENER_H
#include <sys/select.h>
#include <netinet/in.h>
#include "table.h"

struct Listener {
    /* Configuration fields */
    struct sockaddr_storage addr;
    size_t addr_len;
    enum Protocol {
        TLS,
        HTTP
    } protocol;
    char table_name[TABLE_NAME_LEN];

    /* Runtime fields */
    int sockfd;
    const char *(*parse_packet)(const char *, int);
    void (*close_client_socket)(int);
    struct Table *table;
    SLIST_ENTRY(Listener) entries;
};

void init_listeners();
void free_listeners();
struct Listener *add_listener(const struct sockaddr *, size_t, int, const char *);
int fd_set_listeners(fd_set *, int);
void handle_listeners(fd_set *);

static inline int lookup_server_socket(const struct Listener *listener, const char *hostname) {
    return lookup_table_server_socket(listener->table, hostname);
}
#endif
