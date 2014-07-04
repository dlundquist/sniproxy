
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


