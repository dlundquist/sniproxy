#ifndef UTIL_H
#define UTIL_H

#include <sys/socket.h>

size_t parse_address(struct sockaddr_storage*, const char*, int);
void hexdump(const void *, int);
int isnumeric (const char *);

#endif
