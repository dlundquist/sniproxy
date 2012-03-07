#ifndef BINDER_H
#define BINDER_H

#include <sys/socket.h>

void start_binder();
int bind_socket(struct sockaddr *, size_t);
void stop_binder();

#endif
