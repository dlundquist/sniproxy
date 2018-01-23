/*
 * Copyright (c) 2012, Dustin Lundquist <dustin@null-ptr.net>
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h> /* memcpy() */
#include <errno.h> /* errno */
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include "binder.h"
#include "logger.h"

/*
 * binder is a child process we spawn before dropping privileges that is
 * responsible for creating new bound sockets to low ports
 */

static void binder_main(int);
static int parse_ancillary_data(struct msghdr *);


struct binder_request {
    size_t address_len;
    struct sockaddr address[];
};


static int binder_sock = -1; /* socket to binder */
static pid_t binder_pid = -1;


void
start_binder() {
    int sockets[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
        err("sockpair: %s", strerror(errno));
        return;
    }

    pid_t pid = fork();
    if (pid == -1) { /* error case */
        err("fork: %s", strerror(errno));
        close(sockets[0]);
        close(sockets[1]);
    } else if (pid == 0) { /* child */
        /* don't leak file descriptors to the child process */
        for (int i = 0; i < sockets[1]; i++)
            close(i);

        binder_main(sockets[1]);
        exit(0);
    } else { /* parent */
        close(sockets[1]);
        binder_sock = sockets[0];
        binder_pid = pid;
    }
}

int
bind_socket(const struct sockaddr *addr, size_t addr_len) {
    struct binder_request *request;
    struct msghdr msg;
    struct iovec iov[1];
    char control_buf[64];
    char data_buf[256];


    if (binder_pid <= 0) {
        err("%s: Binder not started", __func__);
        return -1;
    }

    size_t request_len = sizeof(request) + addr_len;
    if (request_len > sizeof(data_buf))
        fatal("bind_socket: request length %d exceeds buffer", request_len);
    request = (struct binder_request *)data_buf;
    request->address_len = addr_len;
    memcpy(&request->address, addr, addr_len);

    if (send(binder_sock, request, request_len, 0) < 0) {
        err("send: %s", strerror(errno));
        return -1;
    }

    memset(&msg, 0, sizeof(msg));
    iov[0].iov_base = data_buf;
    iov[0].iov_len = sizeof(data_buf);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control_buf;
    msg.msg_controllen = sizeof(control_buf);

    int len = recvmsg(binder_sock, &msg, 0);
    if (len < 0) {
        err("recvmsg: %s", strerror(errno));
        return -1;
    }

    int fd = parse_ancillary_data(&msg);
    if (fd < 0)
        err("binder returned: %.*s", len, data_buf);

    return fd;
}

void
stop_binder() {
    close(binder_sock);

    int status;
    if (waitpid(binder_pid, &status, 0) < 0)
        err("waitpid: %s", strerror(errno));
}


static void
binder_main(int sockfd) {
    for (;;) {
        char buffer[256];
        int len = recv(sockfd, buffer, sizeof(buffer), 0);
        if (len < 0) {
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "recv(): %s", strerror(errno));
            goto error;
        } else if (len == 0) {
            /* socket was closed */
            close(sockfd);
            break;
        } else if (len < (int)sizeof(struct binder_request)) {
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, "Incomplete error:", sizeof(buffer));
            goto error;
        }

        struct binder_request *req = (struct binder_request *)buffer;

        int fd = socket(req->address[0].sa_family, SOCK_STREAM, 0);
        if (fd < 0) {
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "socket(): %s", strerror(errno));
            goto error;
        }

        /* set SO_REUSEADDR on server socket to facilitate restart */
        int on = 1;
        int result = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if (result < 0) {
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "setsockopt SO_REUSEADDR failed: %s", strerror(errno));
            goto error;
        }

        if (bind(fd, req->address, req->address_len) < 0) {
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "bind(): %s", strerror(errno));
            goto error;
        }

        struct msghdr msg;
        struct iovec iov[1];
        struct cmsghdr *cmsg;
        char control_data[64];
        memset(&msg, 0, sizeof(msg));
        memset(&iov, 0, sizeof(iov));
        memset(&control_data, 0, sizeof(control_data));
        iov[0].iov_base = buffer;
        iov[0].iov_len = sizeof(buffer);
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control_data;
        msg.msg_controllen = sizeof(control_data);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
        int *fdptr = (int *)CMSG_DATA(cmsg);
        memcpy(fdptr, &fd, sizeof(fd));
        msg.msg_controllen = cmsg->cmsg_len;

        if (sendmsg(sockfd, &msg, 0) < 0) {
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), "send: %s", strerror(errno));
            goto error;
        }

        close(fd);

        continue;

        error:

        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            err("send: %s", strerror(errno));
            close(sockfd);
            break;
        }
    }
}

static int
parse_ancillary_data(struct msghdr *m) {
    struct cmsghdr *cmsg;
    int fd = -1;
    int *fdptr;

    for (cmsg = CMSG_FIRSTHDR(m); cmsg != NULL; cmsg = CMSG_NXTHDR(m, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            fdptr = (int *)CMSG_DATA(cmsg);
            memcpy(&fd, fdptr, sizeof(fd));
        }
    }

    return fd;
}
