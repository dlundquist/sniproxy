#include <stdio.h>
#include "cfg_tokenizer.h"

#define LEN 256

int main() {
    FILE *cfg;
    char buffer[LEN];
    enum Token token;

    cfg = fopen("../sni_proxy.conf", "r");
    if (cfg == NULL) {
        perror("fopen:");
        return 1;
    }

    while((token = next_token(cfg, buffer, LEN)) != END) {
        switch(token) {
            case ERROR:
                return 2;
            case EOL:
                printf("seperator\n");
                break;
            case OBRACE:
                printf("open block\n");
                break;
            case CBRACE:
                printf("end block\n");
                break;
            case WORD:
                printf("word: %s\n", buffer);
                break;
            case END:
                break;
        }
    }
    fclose(cfg);
}
