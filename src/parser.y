%{
#include "parser.h"


int yylex (void);
void yyerror (const char *);
%}

%token COMMENT
%token USER
%token LISTEN
%token SOCKET

%%

COMMENT: # COMMENT

user: "user" WORD

listen: "listen" socket listen_block

socket: ipv6_socket | ipv4_socket | unix_socket

ipv6_socket: ipv6_addr port | ipv6_addr

ipv4_socket: ipv4_addr port | ipv4_addr

listen_block: '{' listen_options '}'

listen_options: listen_options listen_option | listen_optioN

listen_option: listen_protocol | listen_table

listen_protocol: "proto" protocol_name | "protocol" protocol_namE

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

