%{
#include "parser.h"


int yylex (void);
void yyerror (const char *);
%}

%token

%%

COMMENT: # COMMENT

USER: "user" WORD

LISTEN: "listen" SOCKET LISTEN_BLOCK

SOCKET: IPV6_SOCKET | IPV4_SOCKET | UNIX_SOCKET

IPV6_SOCKET: IPV6_ADDR PORT | IPV6_ADDR

IPV4_SOCKET: IPV4_ADDR PORT | IPV4_ADDR

LISTEN_BLOCK: OPEN_BLOCK LISTEN_OPTIONS CLOSE_BLOCK

LISTEN_OPTIONS: LISTEN_OPTIONS LISTEN_OPTION | LISTEN_OPTION

LISTEN_OPTION: LISTEN_PROTOCOL | LISTEN_TABLE

LISTEN_PROTOCOL: "proto" PROTOCOL_NAME | "protocol" PROTOCOL_NAME

PROTOCOL_NAME: "http" | "tls"

LISTEN_TABLE: "table" TABLE_NAME

TABLE: "table" TABLE_NAME OPEN_BLOCK TABLE_ENTRIES CLOSE_BLOCK | "table" OPEN_BLOCK TABLE_ENTRIES CLOSE_BLOCK

TABLE_ENTRIES: TABLE_ENTRIES TABLE_ENTRY | TABLE_ENTRY

TABLE_ENTRY: HOSTNAME SOCKET

/*
# blocks are dilimited with {...}
listen 80 {
    proto http
    table http_hosts
}

listen 443 {
    proto tls
    table https_hosts
}

listen 192.0.2.10 80 {
    protocol http
    # this will use default table
}

listen 2001:0db8::10 80 {
    protocol http
    # this will use default table
}

listen unix:/var/run/proxy.sock {
    protocol http
    # this will use default table
}

# named tables are defined with the table directive
table http_hosts {
    example.com 192.0.2.10
}

# named tables are defined with the table directive
table https_hosts {
    # when proxying to sockets you should use different tables since the socket server most likely will not autodetect TLS or HTTP
    example.org unix:/var/run/server.sock
}

# if no table specified the default 'default' table is defined
table {
    # if no port is included default http (80) and https (443) ports are assumed
    example.com 192.0.2.10
    example.net 192.0.2.20
}
*/


%%
