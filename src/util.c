/*
 * Copyright (c) 2011 and 2012, Dustin Lundquist <dustin@null-ptr.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
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
parse_address(struct sockaddr_storage* saddr, const char* address, int port) {
    union {
        struct sockaddr_storage *storage;
        struct sockaddr_in *sin;
        struct sockaddr_in6 *sin6;
        struct sockaddr_un *sun;
    } addr;
    addr.storage = saddr;

    memset(addr.storage, 0, sizeof(struct sockaddr_storage));
    if (inet_pton(AF_INET, address, &addr.sin->sin_addr) == 1) {
        addr.sin->sin_family = AF_INET;
        addr.sin->sin_port = htons(port);
        return sizeof(struct sockaddr_in);
    }

    /* rezero addr incase inet_pton corrupted it while trying to parse IPv4 */
    memset(addr.storage, 0, sizeof(struct sockaddr_storage));
    if (inet_pton(AF_INET6, address, &addr.sin6->sin6_addr) == 1) {
        addr.sin6->sin6_family = AF_INET6;
        addr.sin6->sin6_port = htons(port);
        return sizeof(struct sockaddr_in6);
    }

    memset(addr.storage, 0, sizeof(struct sockaddr_storage));
    if (strncmp("unix:", address, 5) == 0) {
        addr.sun->sun_family = AF_UNIX;
        strncpy(addr.sun->sun_path, address + 5, UNIX_PATH_MAX);
        return offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun->sun_path);
    }

    return 0;
}
