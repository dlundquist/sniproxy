#ifndef CFG_PARSER
#define CFG_PARSER

struct Keyword {
    const char *keyword;
    void *(*create)();
    int (*parse_arg)(void *, char *);
    struct Keyword *block_grammar;
    int (*finalize)(void *, void *);
};


int parse_config(void *, FILE *, const struct Keyword *);


#endif
