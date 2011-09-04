#ifndef TLS_H
#define TLS_H

const char *parse_tls_header(const char *, int);
void close_tls_socket(int);

#endif
