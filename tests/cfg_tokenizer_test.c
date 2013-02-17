#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "cfg_tokenizer.h"


struct Result {
    enum Token type;
    char *value;
};

struct Test {
    char *config;
    struct Result *results;
    int len;
};

char config1[] = "# Comment\n"
                 "numbers {\n"
                 "   one\n"
                 "   two\n"
                 "   three\n"
                 "}";
struct Result results1[] = {
    { EOL, NULL },
    { WORD, "numbers" },
    { OBRACE, NULL },
    { EOL, NULL },
    { WORD, "one" },
    { EOL, NULL },
    { WORD, "two" },
    { EOL, NULL },
    { WORD, "three" },
    { EOL, NULL },
    { CBRACE, NULL },
    { END, NULL },
};

struct Test tests[] = {
    { config1, results1, sizeof(results1) / sizeof(struct Result) },
    { NULL, NULL, 0 } /* End of tests */
};

int main() {
    FILE *cfg;
    char buffer[256];
    enum Token token;
    struct Test *test;
    int i;

    cfg = tmpfile();
    if (cfg == NULL) {
        perror("tmpfile");
        return 1;
    }

    for (test = tests; test->config; test++) {
        fprintf(cfg, "%s", test->config);
        rewind(cfg);

        for (i = 0; i < test->len; i++) {
            token = next_token(cfg, buffer, sizeof(buffer));
            assert(token == test->results[i].type);
            if (test->results[i].value)
                assert(strncmp(buffer, test->results[i].value, sizeof(buffer)) == 0);
        }
        rewind(cfg);
    }

    fclose(cfg);
    return (0);
}
