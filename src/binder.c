#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h> /* memcpy() */
#include <errno.h> /* errno */
#include "binder.h"

/*
 * binder is a child process we spawn before dropping privileges that is responable for creating new bound sockets to low ports
 */


static int binder_sock; /* socket to binder */
static pid_t binder_pid;


static void run_binder(char *, int);
static int parse_ancillary_data(struct msghdr *);


struct binder_request {
    struct sockaddr_storage address;
    size_t address_len;
};


void
start_binder(char *arg0) {
    int sockets[2];
    pid_t pid;

    /* Do not start binder if we are not running as root */
    /*
    if (getuid() != 0)
        return;
    */

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
        perror("sockpair()");
        return;
    }

    pid = fork();
    if (pid == -1) { /* error case */
        perror("fork()");
        close(sockets[0]);
        close(sockets[1]);
        return;
    } else if (pid == 0) { /* child */
        close(sockets[0]);
        run_binder(arg0, sockets[1]);
        exit(0);
    } else { /* parent */
        close(sockets[1]);
        binder_sock = sockets[0];
        binder_pid = pid;
    }
}

int
bind_socket(struct sockaddr *addr, size_t addr_len) {
    struct binder_request request;
    struct msghdr msg;
    struct iovec iov[1];
    char data_buf[256];
    char control_buf[64];

    if (binder_pid <= 0) {
        fprintf(stderr, "Binder not started\n");
        return -1;
    }

    if (addr_len > sizeof(request.address)) {
        fprintf(stderr, "addr_len is too long\n");
        return -1;
    }
    memcpy(&request.address, addr, addr_len);
    request.address_len = addr_len;

    if (send(binder_sock, &request, sizeof(request), 0) < 0) {
        perror("send()");
        return -1;
    }


    memset(&msg, 0, sizeof(msg));
    iov[0].iov_base = data_buf;
    iov[0].iov_len = sizeof(data_buf);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control_buf;;
    msg.msg_controllen = sizeof(control_buf);

    if (recvmsg(binder_sock, &msg, 0) < 0) {
        perror("recvmsg()");
        return -1;
    }

    return parse_ancillary_data(&msg);
}

void
stop_binder() {
    close(binder_sock);

    /* wait() */
}


/* This function is invoked right after the binder is forked */
static void run_binder(char *arg0, int sock_fd) {
    int running = 1, fd, len;
    int *fdptr;
    struct msghdr msg;
    struct iovec iov[1];
    struct cmsghdr *cmsg;
    char control_data[64];
    char buffer[256];

    printf("Binder started\n");

    while (running) {
        len = recv(sock_fd, buffer, sizeof(buffer), 0);
        if (len < 0) {
            perror("recv()");
            return;
        }

        if (len < sizeof(struct binder_request)) {
            fprintf(stderr, "Incomplete request\n");
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, "Incomplete error:", sizeof(buffer));
            goto error;
        }

        struct binder_request *req = (struct binder_request *)buffer;

        fd = socket(req->address.ss_family, SOCK_STREAM, 0);
        if (fd < 0) {
            perror("ERROR opening socket:");
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, "ERROR opening socket:", sizeof(buffer));
            goto error;
        }

        if (bind(fd, (struct sockaddr *)&(req->address.ss_family), req->address_len) < 0) {
            perror("ERROR on binding:");
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, "ERROR on binding:", sizeof(buffer));
            goto error;
        }

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
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        fdptr = (int *)CMSG_DATA(cmsg);
        *fdptr = fd;
        memcpy(fdptr, &fd, sizeof(fd));
        msg.msg_controllen = cmsg->cmsg_len;

        if (sendmsg(sock_fd, &msg, 0) < 0) {
            perror("sendmsg()");
            return;
        }

        close(fd);

        continue;

error:
        strncpy(buffer + strlen(buffer), strerror(errno), sizeof(buffer) - strlen(buffer));

        if (send(sock_fd, buffer, strlen(buffer), 0) < 0) {
            perror("send()");
            return;
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

