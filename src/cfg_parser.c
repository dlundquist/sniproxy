#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfg_parser.h"
#include "cfg_tokenizer.h"

static const struct Keyword * find_keyword(const struct Keyword *, const char *);


int
parse_config(void *context, FILE *cfg, const struct Keyword *grammar) {
    enum Token token;
    char buffer[256];
    const struct Keyword *keyword = NULL;
    void *sub_context = NULL;
    int rv;

    while((token = next_token(cfg, buffer, sizeof(buffer))) != END) {
        switch(token) {
            case ERROR:
                fprintf(stderr, "tokenizer error\n");
                return -1;
            case WORD:
                if (keyword && sub_context && keyword->parse_arg) {
                    rv = keyword->parse_arg(sub_context, buffer, sizeof(buffer));
                    if (rv <= 0)
                        return rv;
                } else if ((keyword = find_keyword(grammar, buffer))) {
                    if (keyword->create)
                        sub_context = keyword->create(context);
                    else
                        sub_context = context;

                    if (sub_context == NULL) {
                        fprintf(stderr, "failed to create subcontext\n");
                        return -1;
                    }

                    /* Special case for wildcard grammers i.e. tables */
                    if (keyword->keyword == NULL && keyword->parse_arg) {
                        rv = keyword->parse_arg(sub_context, buffer, sizeof(buffer));
                        if (rv <= 0)
                            return rv;
                    }

                } else {
                    fprintf(stderr, "unknown keyword %s\n", buffer);
                    return -1;
                }
                break;
            case OBRACE:
                if (keyword && sub_context && keyword->block_grammar) {
                    rv = parse_config(sub_context, cfg, keyword->block_grammar);
                    if (rv <= 0)
                        return rv;
                } else {
                    printf("block without context\n");
                    return -1;
                }
                break;
            case EOL:
                if (keyword && sub_context && keyword->finalize) {
                    rv = keyword->finalize(sub_context);
                    if (rv <= 0)
                        return rv;
                }

                keyword = NULL;
                sub_context = NULL;
                break;
            case CBRACE:
                /* Finalize the current subcontext before returning */
                if (keyword && sub_context && keyword->finalize) {
                    rv = keyword->finalize(sub_context);
                    if (rv <= 0)
                        return rv;
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

    while (grammar->keyword) {
        if (strncmp(grammar->keyword, word, strlen(word)) == 0)
            return grammar;

        grammar++;
    }

    /* Special case for wildcard grammers i.e. tables */
    if (grammar->keyword == NULL && grammar->create != NULL)
        return grammar;

    return NULL;
}
