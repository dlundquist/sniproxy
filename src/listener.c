#include <sys/types.h>
#include <sys/queue.h>
#include <unistd.h>
#include <syslog.h>
#include "listener.h"
#include "connection.h"

#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))

static SLIST_HEAD(, Listener) listeners;

void
init_listeners() {
    SLIST_INIT(&listeners);
}

/*
 * Prepares the fd_set as a set of all active file descriptors in all our
 * currently active connections and one additional file descriptior fd that
 * can be used for a listening socket.
 * Returns the highest file descriptor in the set.
 */
int
fd_set_listeners(fd_set *fds, int max) {
    struct Listener *iter;

    SLIST_FOREACH(iter, &listeners, entries) {
        if (iter->sockfd > FD_SETSIZE) {
            syslog(LOG_WARNING, "File descriptor > than FD_SETSIZE\n");
            break;
        }

        FD_SET(iter->sockfd, fds);
        max = MAX(max, iter->sockfd);
    }

    return max;
}

void
handle_listeners(fd_set *rfds) {
    struct Listener *iter;

    SLIST_FOREACH(iter, &listeners, entries) {
        if (FD_ISSET (iter->sockfd, rfds))
            accept_connection(iter);
    }
}
