#ifndef CFG_PARSER
#define CFG_PARSER

struct Keyword {
    const char *keyword;
    void *(*create)(void *);
    int (*parse_arg)(void *, char *, size_t);
    struct Keyword *block_syntax;
    int (*finalize)(void *);
};


int parse_config(void *, FILE *, struct Keyword *);


#endif
