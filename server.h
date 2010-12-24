#ifndef SERVER_H
#define SERVER_H

size_t parse_address(struct sockaddr_storage*, const char*, int);
int init_server(const char *, int);
void run_server(int);

#endif
