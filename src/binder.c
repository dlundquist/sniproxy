#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "binder.h"

/*
 * binder is a child process we spawn before dropping privileges that is responable for creating new bound sockets to low ports
 */


static int binder_sock; /* socket to binder */


static void run_binder(char *, int);

struct binder_msg {
};


void
start_binder(char *arg0) {
    int sockets[2];
    pid_t pid;
    
    /* Do not start binder if we are not running as root */
    if (getuid() != 0)
        return;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
        perror("sockpair()");
        return;
    }

    pid = fork();
    if (pid == -1) { /* error case */
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
    }
}

int
bind_socket(struct sockaddr *addr, int addr_len) {
    
    sendmsg();

    recvmsg();

}

void
stop_binder() {
    close(binder_sock);

    /* wait() */
}


/* This function is invoked right after the binder is forked */
static void run_binder(char *arg0, int sock_fd) {
    int running = 1;



    while (running) {
        recvmsg();
        
    }
}
