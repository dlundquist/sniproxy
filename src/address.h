#ifndef ADDRESS_H
#define ADDRESS_H

#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>

struct Address;

struct Address *new_address(const char *);
struct Address *new_address_sa(const struct sockaddr *, socklen_t);
size_t address_len(const struct Address *);
int address_is_hostname(const struct Address *);
int address_is_sockaddr(const struct Address *);
int address_is_wildcard(const struct Address *);
const char *address_hostname(const struct Address *);
const struct sockaddr *address_sa(const struct Address *);
socklen_t address_sa_len(const struct Address *);
int address_port(const struct Address *);
void address_set_port(struct Address *, int);
const char *display_address(const struct Address *, char *, size_t);
const char *display_sockaddr(const void *, char *, size_t);
int is_numeric(const char *);

#endif
