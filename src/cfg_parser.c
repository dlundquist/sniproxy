#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfg_parser.h"
#include "cfg_tokenizer.h"


struct Keyword {
    const char *keyword;
    void *(*create)(void *);
    int (*parse_arg)(void *, char *, size_t);
    struct Keyword *block_syntax;
    int (*finalize)(void *);
};

struct Keyword *syntax;

int
parse_config(void *context, FILE *cfg) {
    enum Token token;
    struct Keyword *keyword = NULL;
    void *keyword_obj = NULL;
    char buffer[256];

    while((token = next_token(cfg, buffer, sizeof(buffer))) != END) {
        switch(token) {
            case ERROR:
                return -1;
            case EOL:
                if (keyword != NULL && keyword_obj != NULL)
                    keyword->finalize(keyword_obj);
                keyword = NULL;
                keyword_obj = NULL;
                break;
            case WORD:
                if (keyword == NULL) {
                    for(struct Keyword *iter = syntax; keyword_obj == NULL && iter->keyword != NULL; iter++) {
                        if (strncmp(iter->keyword, buffer, strlen(buffer)) == 0) {
                            keyword = iter;
                            keyword_obj = keyword->create(context);
                        }            
                    }
                    if (keyword == NULL || keyword_obj == NULL) {
                        printf("unknown keyword %s\n", buffer);
                        exit(1);
                    }
                } else {
                    keyword->parse_arg(keyword_obj, buffer, sizeof(buffer));
                }
                break;
            case OBRACE:
                if (keyword != NULL && keyword_obj != NULL)
                    parse_config(context, cfg);
                else {
                    printf("block without context\n");
                    exit(1);
                }
                break;
            case CBRACE:
                if (keyword != NULL && keyword_obj != NULL)
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
