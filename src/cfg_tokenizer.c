#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "cfg_tokenizer.h"

#define BUFFER_SIZE 256

static void chomp_line(FILE *);
static int next_word(FILE *, char *, int);


/*
 * next_token() returns the next token based on the current position of config file
 * advancing the position to immidiatly after the token.
 */
enum Token
next_token(FILE *config, char *buffer, size_t buffer_len) {
    int ch;
    int token_len;

    assert(config != NULL);

    while ((ch = getc(config)) != EOF) {
        switch(ch) {
            case ' ':
                /* fall through */
            case '\t':
                /* no op */
                break;
            case '#': /* comment */
                chomp_line(config);
                /* fall through */
            case ';':
                /* fall through */
            case '\n':
                /* fall through */
            case '\r':
                return EOL;
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

                return WORD;
        }
    }
    return END;
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
            /* Seperators */
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


