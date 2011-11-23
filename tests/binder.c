#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "binder.h"

int test_binder(int);

int main() {
    int i;

    start_binder("binder_test");
    for (i = 8080; i < 8090; i++)
        test_binder(i);

    stop_binder();

    return 0;
}

int
test_binder(int port) {
    int fd;
    struct sockaddr_in addr;
    struct sockaddr_storage addr_verify;
    socklen_t len;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    fd = bind_socket((struct sockaddr *)&addr, sizeof(addr));

    assert(fd > 0);

    /* Verify we obtained the expected socket address */
    len = sizeof(addr_verify);
    if (getsockname(fd, (struct sockaddr *)&addr_verify, &len) < 0) {
        perror("getsockname:");
        exit(1);
    }

    assert(addr.sin_family == ((struct sockaddr_in *)&addr_verify)->sin_family);
    assert(addr.sin_addr.s_addr == ((struct sockaddr_in *)&addr_verify)->sin_addr.s_addr);
    assert(addr.sin_port == ((struct sockaddr_in *)&addr_verify)->sin_port);

    /* Verify we can listen to it */
    if (listen(fd, 5) < 0) {
        perror("listen:");
        exit(1);
    }

    return 0;
}
