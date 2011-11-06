#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "table.h"

static const char *config_file;

int
init_config(const char *config) {
    config_file = config;
    init_tables();
    return load_config();
}

void
free_config() {
    free_tables();
}

int
load_config() {
    FILE *config;
    int count = 0;
    char line[256];
    char *hostname;
    char *address;
    char *port;
    struct Table *default_table;

    if (config_file == NULL)
        return -1;

    config = fopen(config_file, "r");
    if (config == NULL) {
        fprintf(stderr, "Unable to open %s\n", config_file);
        return -1;
    }

    default_table = lookup_table("default");
    if (default_table == NULL)
        default_table = add_table("default");

    while (fgets(line, sizeof(line), config) != NULL) {
        /* skip blank and comment lines */
        if (line[0] == '#' || line[0] == '\n')
            continue;
	
        hostname = strtok(line, " \t");
        if (hostname == NULL)
            goto fail;

        address = strtok(NULL , " \t");
        if (address == NULL)
            goto fail;

        port = strtok(NULL , " \t");
        if (port == NULL)
            goto fail;

        add_table_backend(default_table, hostname, address, atoi(port));
        count ++;
        continue;

    fail:
        fprintf(stderr, "Error parsing line: %s", line);
    }

    fclose(config);
    return count;
}
