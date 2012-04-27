

          +---------------+
          |Config:        |
          |  config_file  |
          |  username     |
          +---------------+
            |          \----------\
            v                     |
    +-----------+                 v
    |Listener:  |+            +-------+             +------------+
    |  socket   ||            |Table: |+            |Backend:    |+
    |  protocol ||--has one-->|  name ||--has many->|  hostname* ||+
    +-----------+|            +-------+|            |  address   |||
     +-----------+             +-------+            |  port      |||
        ^                                           +------------+||
        |             +-------------+                +------------+|
        |             |Connection:  |+                +------------+
        |             |  state      ||+                  ^
        \-referneces--|  listener   |||                  |
                      |  client     |||                  |
                      |    socket   |||                  |
                      |    buffer   |||                  |
                      |  server     |||--selected from---/
                      |    socket   |||
                      |    buffer   |||
                      +-------------+||
                       +-------------+|
                        +-------------+

Listeners are listening service ports, each has an associated address, port
and protocol and socket.  When an incomming connection is accepted on the
socket, a new connection object is created. The first packet is inspect
and the hostname is extracted from the TLS Client Hello or HTTP Request
(depending on protocol selected). The listen's table is consulted for backend
maching the requested hostnamer, this match may be simple maching strings or
regular expressions. A second server connection is established to the address
and port specified by the backend, and the initial packet is forwarded to over
this second socket. Form this point on, when a packet is received from either
the client or server, its contents is buffered and sent through the other
socket. When either the client or server closes the socket, the buffer to
the other socket is sent and the connection is closed. After both sockets
have been closed the connection is removed.

