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
#include <string.h>
#include "cfg_parser.h"
#include "cfg_tokenizer.h"

static const struct Keyword *find_keyword(const struct Keyword *, const char *);


int
parse_config(void *context, FILE *cfg, const struct Keyword *grammar) {
    enum Token token;
    char buffer[256];
    const struct Keyword *keyword = NULL;
    void *sub_context = NULL;
    int result;

    while ((token = next_token(cfg, buffer, sizeof(buffer))) != END) {
        switch (token) {
            case ERROR:
                fprintf(stderr, "tokenizer error\n");
                return -1;
            case WORD:
                if (keyword && sub_context && keyword->parse_arg) {
                    result = keyword->parse_arg(sub_context, buffer);
                    if (result <= 0)
                        return result;

                } else if ((keyword = find_keyword(grammar, buffer))) {
                    if (keyword->create)
                        sub_context = keyword->create();
                    else
                        sub_context = context;

                    if (sub_context == NULL) {
                        fprintf(stderr, "failed to create subcontext\n");
                        return -1;
                    }

                    /* Special case for wildcard grammars i.e. tables */
                    if (keyword->keyword == NULL && keyword->parse_arg) {
                        result = keyword->parse_arg(sub_context, buffer);
                        if (result <= 0)
                            return result;
                    }

                } else {
                    fprintf(stderr, "unknown keyword %s\n", buffer);
                    return -1;
                }
                break;
            case OBRACE:
                if (keyword && sub_context && keyword->block_grammar) {
                    result = parse_config(sub_context, cfg, keyword->block_grammar);
                    if (result <= 0)
                        return result;
                } else {
                    fprintf(stderr, "block without context\n");
                    return -1;
                }
                break;
            case EOL:
                if (keyword && sub_context && keyword->finalize) {
                    result = keyword->finalize(context, sub_context);
                    if (result <= 0)
                        return result;
                }

                keyword = NULL;
                sub_context = NULL;
                break;
            case CBRACE:
                /* Finalize the current subcontext before returning */
                if (keyword && sub_context && keyword->finalize) {
                    result = keyword->finalize(context, sub_context);
                    if (result <= 0)
                        return result;
                }

                /* fall through */
            case END:
                return 1;
        }
    }
    return 1;
}

static const struct Keyword *
find_keyword(const struct Keyword *grammar, const char *word) {
    for (grammar; grammar->keyword; grammar++)
        if (strncmp(grammar->keyword, word, strlen(word)) == 0)
            return grammar;

    /* Special case for wildcard grammars i.e. tables */
    if (grammar->keyword == NULL && grammar->create)
        return grammar;

    return NULL;
}
