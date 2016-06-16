%{

    int yyerror(char *s);
    int yylex(void);

%}

%union {
    int number;
    char *string;
    char ***vector;
    struct Config *config;
}

%token	TOKEN_EOL
%token	TOKEN_OBRACE
%token	TOKEN_CBRACE
%token	<string> TOKEN_WORD

%type <vector> nameservers
%type <vector> search_domains
%type <config> input
%type <config> statement

%start  input

%%

input: statement
     | input TOKEN_EOL statement { merge_configs($1, $3) }

statement: "username" TOKEN_WORD { $$ = new_config(); $$->user = strdup($2) }
         | "pidfile" TOKEN_WORD { $$ = new_config(); $$->pidfile = strdup($2) }
         | "resolver" TOKEN_OBRACE resolver_stmts TOKEN_CBRACE
         | "error_log" TOKEN_OBRACE log_stmts TOKEN_CBRACE
         | "access_log" TOKEN_OBRACE log_stmts TOKEN_CBRACE
         | "listener" TOKEN_OBRACE listener_stmts TOKEN_CBRACE
         | "listener" TOKEN_WORD TOKEN_OBRACE listener_stmts TOKEN_CBRACE
         | "listener" TOKEN_WORD TOKEN_WORD TOKEN_OBRACE listener_stmts TOKEN_CBRACE
         | "table" TOKEN_WORD TOKEN_OBRACE table_stmts TOKEN_CBRACE
         | "table" TOKEN_OBRACE table_stmts TOKEN_CBRACE
         | %empty

resolver_stmts: resolver_stmt
              | resolver_stmts TOKEN_EOL resolver_stmt

resolver_stmt: "nameserver" nameservers
             | "search" search_domains
             | "mode" TOKEN_WORD
             | %empty

nameservers: TOKEN_WORD { append_to_string_vector($$, $1); }
           | nameservers TOKEN_WORD { append_to_string_vector($1, $2) }

search_domains: TOKEN_WORD { append_to_string_vector($$, $1); }
              | search_domains TOKEN_WORD { append_to_string_vector($1, $2) }

log_stmts: log_stmt
         | log_stmts TOKEN_EOL log_stmt

log_stmt: "filename" TOKEN_WORD
        | "syslog" TOKEN_WORD
        | "priority" TOKEN_WORD
        | %empty

listener_stmts: listener_stmt
              | listener_stmts TOKEN_EOL listener_stmt

listener_stmt: "protocol" TOKEN_WORD
             | "table" TOKEN_WORD
             | "fallback" TOKEN_WORD
             | "source" TOKEN_WORD
             | "access_log" TOKEN_WORD
             | "access_log" TOKEN_OBRACE log_stmts TOKEN_CBRACE
             | "bad_requests" TOKEN_WORD
             | %empty

table_stmts: table_stmt
           | table_stmts TOKEN_EOL table_stmt

table_stmt: TOKEN_WORD
          | TOKEN_WORD TOKEN_WORD
          | TOKEN_WORD TOKEN_WORD TOKEN_WORD
          | %empty

%%

