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
    int result;

    while((token = next_token(cfg, buffer, sizeof(buffer))) != END) {
        switch(token) {
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
                        sub_context = keyword->create(context);
                    else
                        sub_context = context;

                    if (sub_context == NULL) {
                        fprintf(stderr, "failed to create subcontext\n");
                        return -1;
                    }

                    /* Special case for wildcard grammers i.e. tables */
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
                    printf("block without context\n");
                    return -1;
                }
                break;
            case EOL:
                if (keyword && sub_context && keyword->finalize) {
                    result = keyword->finalize(sub_context);
                    if (result <= 0)
                        return result;
                }

                keyword = NULL;
                sub_context = NULL;
                break;
            case CBRACE:
                /* Finalize the current subcontext before returning */
                if (keyword && sub_context && keyword->finalize) {
                    result = keyword->finalize(sub_context);
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

    while (grammar->keyword) {
        if (strncmp(grammar->keyword, word, strlen(word)) == 0)
            return grammar;

        grammar++;
    }

    /* Special case for wildcard grammers i.e. tables */
    if (grammar->keyword == NULL && grammar->create)
        return grammar;

    return NULL;
}
