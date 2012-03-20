#ifndef CFG_TOKENIZER
#define CFG_TOKENIZER

#include <stdio.h>

enum Token {
    ERROR,
    EOL,
    OBRACE,
    CBRACE,
    WORD,
    END,
};

enum Token next_token(FILE *, char *, size_t);

#endif
