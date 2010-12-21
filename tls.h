#ifndef TLS_H
#define TLS_H

#include <stdint.h>

const char *parse_tls_header(const uint8_t *, int);
void close_tls_socket(int);

#endif
