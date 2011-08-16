#include <stdio.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h> /* memset */
#include <sys/socket.h> /* sockaddr_storage */
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "util.h"

#define UNIX_PATH_MAX 108


void
hexdump(const void *ptr, int buflen) {
    const unsigned char *buf = (const unsigned char*)ptr;
    int i, j;
    for (i=0; i<buflen; i+=16) {
        printf("%06x: ", i);
        for (j=0; j<16; j++) 
            if (i+j < buflen)
                printf("%02x ", buf[i+j]);
            else
                printf("   ");
        printf(" ");
        for (j=0; j<16; j++) 
            if (i+j < buflen)
                printf("%c", isprint(buf[i+j]) ? buf[i+j] : '.');
        printf("\n");
    }
}

size_t
parse_address(struct sockaddr_storage* addr, const char* address, int port) {

    memset(addr, 0, sizeof(struct sockaddr_storage));
    if (inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1) {
        ((struct sockaddr_in *)addr)->sin_family = AF_INET;
        ((struct sockaddr_in *)addr)->sin_port = htons(port);
        return sizeof(struct sockaddr_in);
    }

    memset(addr, 0, sizeof(struct sockaddr_storage));
    if (inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1) {
        ((struct sockaddr_in6 *)addr)->sin6_family = AF_INET6;
        ((struct sockaddr_in6 *)addr)->sin6_port = htons(port);
        return sizeof(struct sockaddr_in6);
    }

    if (strncmp("unix:", address, 5) == 0) {
        memset(addr, 0, sizeof(struct sockaddr_storage));
        ((struct sockaddr_un *)addr)->sun_family = AF_UNIX;
        strncpy(((struct sockaddr_un *)addr)->sun_path, address + 5, UNIX_PATH_MAX);

        return offsetof(struct sockaddr_un, sun_path) + strlen(((struct sockaddr_un *)addr)->sun_path);
    }


    return 0;
}
