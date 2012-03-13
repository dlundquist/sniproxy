#include "config.h"

int main() {
    struct Config *config;

    config = init_config("../sni_proxy.conf");

    print_config(config);

    return 0;
}
