#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "config.h"
#include "table.h"

#define BUFFER_SIZE 256

enum Token {
    ERROR,
    SEPERATOR,
    OBRACE,
    CBRACE,
    LISTEN,
    USER,
    PROTOCOL,
    TABLE,
    WORD,
    ENDCONFIG,
};


static int parse_listen_stanza(struct Config *, FILE *);
static int parse_table_stanza(struct Config *, FILE *);
static enum Token next_token(FILE *, char *, size_t);
static void chomp_line(FILE *);
static int next_word(FILE *, char *, int);


struct Config *
init_config(const char *filename) {
    struct Config *c;

    c = calloc(1, sizeof(struct Config));
    if (c == NULL) {
        perror("malloc()");
        return NULL;
    }

    c->filename = strdup(filename);
    if (c->filename == NULL) {
        free(c);
        perror("malloc()");
        return NULL;
    }

    reload_config(c);

    return(c);
}

void
free_config(struct Config *c) {
    assert(c != NULL);

    free(c->filename);
    /* TODO free nested objects */

    free(c);
}


int
reload_config(struct Config *c) {
    FILE *config;
    int done = 0;
    enum Token token;
    char buffer[BUFFER_SIZE];

    assert(c != NULL);
    assert(c->filename != NULL);

    config = fopen(c->filename, "r");
    if (config == NULL) {
        fprintf(stderr, "Unable to open %s\n", c->filename);
        return -1;
    }

    while(done == 0) {
        token = next_token(config, buffer, sizeof(buffer));
        switch (token) {
            case SEPERATOR:
                /* no op */
                break;
            case USER:
                if (c->user != NULL)
                    fprintf(stderr, "Warning: user already specified\n");
               
                token = next_token(config, buffer, sizeof(buffer));
                if (token != WORD) {
                    fprintf(stderr, "Error parsing config near %s\n", buffer);
                    return -1;
                }

                c->user = strdup(buffer);
                if (c->user == NULL) {
                    perror("malloc()");
                    return -1;
                }
                break;
            case LISTEN:
                parse_listen_stanza(c, config);
                break;
            case TABLE:
                parse_table_stanza(c, config);
                break;
            case ENDCONFIG:
                done = 1;
                break;
            default:
                fprintf(stderr, "Error parsing config near %s\n", buffer);
                return -1;
        }
    }
    fclose(config);


    /* TODO validate config */

}

static int
parse_listen_stanza(struct Config *c, FILE *file) {
    enum {
        ADDRESS,
        BLOCK,
        PROTOCOL,
        TABLE,
        DONE
    } state = ADDRESS;
    enum Token token;
    char buffer[BUFFER_SIZE];
    struct Listener *listener;

    listener = calloc(1, sizeof(struct Listener));
    if (listener == NULL) {
        fprintf(stderr, "malloc failed\n");
        return -1;
    }

    while(state != DONE) {
        token = next_token(file, buffer, sizeof(buffer));
        switch(state) {
            case ADDRESS:
                switch(token) {
                    case OBRACE:
                        state = BLOCK;
                        break;
                    case WORD:
                        parse_listener_address_token(listener, buffer);
                        break;
                    default:
                        return -1;
                }
                break;
            case BLOCK:
                switch(token) {
                    case SEPERATOR:
                        break;
                    case TABLE:
                        state = TABLE;
                        break;
                    case PROTOCOL:
                        state = PROTOCOL;
                        break;
                    case CBRACE:
                        state = DONE;
                        break;
                    default:
                        return -1;
                }
                break;
            case PROTOCOL:
                switch(token) {
                    case SEPERATOR:
                        state = BLOCK;
                        break;
                    case WORD:
                        parse_listener_protocol_token(listener, buffer);
                        break;
                    default:
                        return -1;
                }
            case TABLE:
                switch(token) {
                    case SEPERATOR:
                        state = BLOCK;
                        break;
                    case WORD:
                        parse_listener_table_token(listener, buffer);
                        break;
                    default:
                        return -1;
                }
        }
    }
    if (valid_listener(listener) <= 0)
        return -1;
     
}

static int
parse_table_stanza(struct Config *c, FILE *file) {
    enum {
        NAME,
        BLOCK,
        DONE
    } state;
}



/*
 * next_token() returns the next token based on the current position of config file
 * advancing the position to immidiatly after the token.
 */
static enum Token
next_token(FILE *config, char *buffer, size_t buffer_len) {
    int ch;
    int token_len;

    assert(config != NULL);

    while ((ch = getc(config)) != EOF) {
        switch(ch) {
            case '#': /* comment */
                chomp_line(config);
                /* fall through */
            case ' ':
                /* fall through */
            case '\t':
                /* no op */
                break;
            case ';':
                /* fall through */
            case '\n':
                /* fall through */
            case '\r':
                return SEPERATOR;
            case '{':
                return OBRACE;
            case '}':
                return CBRACE;
            default:
                /* Rewind one byte, so next_word() can fetch the begining of the word */
                fseek(config, -1, SEEK_CUR);

                token_len = next_word(config, buffer, buffer_len);
                if (token_len <= 0)
                    return ERROR;

                if (strncmp("user", buffer, token_len) == 0)
                    return USER;
                if (strncmp("listen", buffer, token_len) == 0)
                    return LISTEN;
                if (token_len > 5 && strncmp("protocol", buffer, token_len - 1) == 0)
                    return PROTOCOL;
                if (strncmp("table", buffer, token_len) == 0)
                    return TABLE;

                return WORD;
        }
    }
    return ENDCONFIG;
}

static void 
chomp_line(FILE *file) {
    int ch;

    while ((ch = getc(file)) != EOF)
        if (ch == '\n' || ch == '\r')
            return;
}

static int
next_word(FILE *file, char *buffer, int buffer_len) {
    int ch;
    int len = 0;
    int quoted = 0;
    int escaped = 0;

    while ((ch = getc(file)) != EOF && len < buffer_len) {
        if (escaped) {
            escaped = 0;
            buffer[len] = (char)ch;
            len ++;
            continue;
        }
        switch(ch) {
            case '\\':
                escaped = 1;
                break;
                case '\"':
                    quoted = 1 - quoted;
                break;
            case ' ':
            case '\t':
            case ';':
            case '\n':
            case '\r':
            case '#':
            case '{':
            case '}':
                if (quoted == 0) {
                    /* rewind the file one character, so we don't eat part of the next token */
                    fseek(file, -1, SEEK_CUR);

                    buffer[len] = '\0';
                    len ++;
                    return len;
                }
                /* fall through */
            default:
                buffer[len] = (char)ch;
                len ++;
        }
    }
    /* We reached the end of the file, or filled our buffer */
    return -1;
}


