#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "table.h"


enum Token {
    ERROR,
    SEPERATOR,
    COMMENT,
    OBRACE,
    CBRACE,
    LISTEN,
    USER,
    PROTOCOL,
    TABLE,
    WORD,
    ENDCONFIG,
};

static const char *config_file;


static enum Token next_token(FILE *, char *, size_t);
static void chomp_line(FILE *);
static int next_word(FILE *, char *, int);



int
init_config(const char *config) {
    config_file = config;
    init_tables();
    return load_config();
}

void
free_config() {
    free_tables();
}

int
load_config() {
    FILE *config;
    int count = 0;
    char line[256];
    char *hostname;
    char *address;
    char *port;
    struct Table *default_table;

    if (config_file == NULL)
        return -1;

    config = fopen(config_file, "r");
    if (config == NULL) {
        fprintf(stderr, "Unable to open %s\n", config_file);
        return -1;
    }

    default_table = lookup_table("default");
    if (default_table == NULL)
        default_table = add_table("default");

    while (fgets(line, sizeof(line), config) != NULL) {
        /* skip blank and comment lines */
        if (line[0] == '#' || line[0] == '\n')
            continue;
	
        hostname = strtok(line, " \t");
        if (hostname == NULL)
            goto fail;

        address = strtok(NULL , " \t");
        if (address == NULL)
            goto fail;

        port = strtok(NULL , " \t");
        if (port == NULL)
            goto fail;

        add_table_backend(default_table, hostname, address, atoi(port));
        count ++;
        continue;

fail:
        fprintf(stderr, "Error parsing line: %s", line);
    }

    fclose(config);
    return count;
}

/*
 * next_token() returns the next token based on the current position of config file
 * advancing the position to immidiatly after the token.
 *
 *
 */
static enum Token
next_token(FILE *config, char *buf, size_t len) {
    int ch;

    if (config == NULL)
        return ERROR;

    while ((ch = getc(config)) != EOF) {
        switch(ch) {
            case '\n':
                /* fall through */
            case '\r':
                return SEPERATOR;
            case ' ':
                /* fall through */
            case '\t':
                /* white space */
                break;
            case '{':
                return OBRACE;
            case '}':
                return CBRACE;
            case '#':
                chomp_line(config);
                return COMMENT;
            default:
                fseek(config, -1, SEEK_CUR);
                if (next_word(config, buf, len) <= 0)
                    return ERROR;
                /* TODO check for tokens: USER, PROTOCOL, LISTEN, TABLE */
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

int
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
            case '\n':
            case '\r':
            case '#':
            case '{':
            case '}':
                if (quoted == 0) {
                    /* rewind the file one character, incase there isn't whitespace after the word */
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
    /* rewind our file to our starting point */
    fseek(file, -len, SEEK_CUR);
    return -1;
}
