
SNIProxy Statistics Interface
=============================

Goals
-----

* examine the active connections
* retrieve statistics per listener:
    * active connections
    * total connections
    * uptime
    * total data received from clients
    * total data sent to clients

While SNIProxy supports binding to an arbitrary TCP socket, this interface is
intended to be used only from the local system and authentication is not
supported.


Protocol
--------



Iteration
---------

Since a stats request may block, the iteration of connections may be
interrupted while connections are added or removed. To get around this we use a
ConnectionIterator:


                                +-------------+
                                | iterator    |
                                +-------------+
                                      | ^
                                      | |
                                      v |
        +-------------+         +-------------+         +-------------+
HEAD--->| connection  |-------->| connection  |-------->| connection  |
        | iterators[] |<--------| iterators[] |<--------| iterators[] |
        +-------------+         +-------------+         +-------------+

Each connection needs to know which iterators are point at it, the number will
usually be very small (i.e. 0 or 1), but it could be a very large number (if
excessive number of very slow requests are made to stats listener). Options:

    1. Fixed array in each connection (can't support large number of iterators)
    2. Linked list of iterators (each iterator can only be in a single linked list)
    3. Some sort of iterator holder node (another distinct object for each iterator)
    4. Variable length array in each connection (handles the zero iterators case nicely)

    struct ConnectionIterator {
        struct Connection *next;
    };

    struct Connection {
        enum State {
            NEW,            /* Before successful accept */
            ACCEPTED,       /* Newly accepted client connection */
            PARSED,         /* Parsed initial request and extracted hostname */
            RESOLVING,      /* DNS query in progress */
            RESOLVED,       /* Server socket address resolved */
            CONNECTED,      /* Connected to server */
            SERVER_CLOSED,  /* Client closed socket */
            CLIENT_CLOSED,  /* Server closed socket */
            CLOSED          /* Both sockets closed */
        } state;

        struct {
            struct sockaddr_storage addr;
            socklen_t addr_len;
            struct ev_io watcher;
            struct Buffer *buffer;
        } client, server;
        struct Listener *listener;
        const char *hostname; /* Requested hostname */
        size_t hostname_len;
        struct ResolvQuery *query_handle;
        ev_tstamp established_timestamp;
        struct {
            size_t count;
            size_t len;
            ConnectionIterator **iterators;
        } iterators;

        LIST_ENTRY(Connection) entries;
    };

    struct Connection *next(struct ConnectionIterator iter) {
        /* Fetch our resulting next connection */
        struct Connection *next = iter->next;
        /* Update next pointer */
        iter->next = LIST_NEXT(next, entries);

        /* Find and remove iterator record from next */

        /* Add iterator record to new_next */


        /* Special case: last connection */

        /* Optimization: reuse/move con->iterators->interators */
    }

    void delete_connection(struct Connection *connection) {
        struct Connection *next = LIST_NEXT(connection, entries);
        for (size_t i = 0; i < connection->iterators.count; i++) {
            ConnectionIterator *iter = connection->iterators->iterators[i];
            /* Update iterator to point to new next *?
            iter->next = next;

            /* Add iterator record to next */
        }
        /* bulk clear iterators from connection */

        /* Optimization: reuse/move con->iterators->interators if next
           interators is empty */
    }
