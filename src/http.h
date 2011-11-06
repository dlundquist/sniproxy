#ifndef HTTP_H
#define HTTP_H

const char *parse_http_header(const char *, int);
void close_http_socket(int);

#endif
