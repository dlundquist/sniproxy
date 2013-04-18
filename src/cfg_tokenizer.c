/*
 * Copyright (c) 2012, Dustin Lundquist <dustin@null-ptr.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include "cfg_tokenizer.h"

static void chomp_line(FILE *);
static int next_word(FILE *, char *, int);


/*
 * next_token() returns the next token based on the current position of
 * configuration file advancing the position to immediately after the token.
 */
enum Token
next_token(FILE *config, char *buffer, size_t buffer_len) {
    int ch;
    int token_len;

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
                /* Rewind one byte, so next_word() can fetch from
                 * the beginning of the word */
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
                quoted = 1 - quoted; /* toggle quoted flag */
                break;
            /* separators */
            case ' ':
            case '\t':
            case ';':
            case '\n':
            case '\r':
            case '#':
            case '{':
            case '}':
                if (quoted == 0) {
                    /* rewind the file one character, so we don't eat
                     * part of the next token */
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

