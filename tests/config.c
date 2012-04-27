#include "config.h"

int main() {
    struct Config *config;

    config = init_config("../sni_proxy.conf");
    if (config == NULL) {
        fprintf(stderr, "Failed to parse config\n");
        return 1;
    }

    print_config(stdout, config);

    free_config(config);

    return 0;
}
