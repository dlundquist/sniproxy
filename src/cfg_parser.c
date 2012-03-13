#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfg_parser.h"
#include "cfg_tokenizer.h"

int
parse_config(void *context, FILE *cfg, struct Keyword *grammar) {
    enum Token token;
    struct Keyword *keyword = NULL;
    void *keyword_obj = NULL;
    char buffer[256];

    while((token = next_token(cfg, buffer, sizeof(buffer))) != END) {
        switch(token) {
            case ERROR:
                return -1;
            case WORD:
                if (keyword == NULL) {
                    for(struct Keyword *iter = grammar; keyword_obj == NULL &&
                            iter->keyword; iter++) {

                        if (strncmp(iter->keyword, buffer, strlen(buffer)) == 0) {
                            keyword = iter;
                            if (keyword->create)
                                keyword_obj = keyword->create(context);
                            else
                                keyword_obj = context;
                        }            
                    }
                    if (keyword == NULL && grammar->keyword == NULL &&
                            grammar->create != NULL) {
                        /* Special case for wildcard grammers i.e. tables */
                        keyword = grammar;
                        keyword_obj = keyword->create(context);
                        keyword->parse_arg(keyword_obj, buffer, sizeof(buffer));

                    } else if (keyword == NULL) {
                        printf("unknown keyword %s\n", buffer);
                        exit(1);
                    }
                } else {
                    if (keyword && keyword_obj && keyword->parse_arg)
                        keyword->parse_arg(keyword_obj, buffer, sizeof(buffer));
                }
                break;
            case OBRACE:
                if (keyword && keyword_obj && keyword->block_grammar)
                    parse_config(keyword_obj, cfg, keyword->block_grammar);
                else {
                    printf("block without context\n");
                    exit(1);
                }
                break;
            case EOL:
                if (keyword && keyword_obj && keyword->finalize)
                    keyword->finalize(keyword_obj);
                keyword = NULL;
                keyword_obj = NULL;
                break;
            case CBRACE:
                if (keyword && keyword_obj && keyword->finalize)
                    keyword->finalize(keyword_obj);
                keyword = NULL;
                keyword_obj = NULL;
                /* fall through */
            case END:
                return 0;
        }
    }
    return 0;
}
