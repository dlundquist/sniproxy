/*
 * Copyright (c) 2011 and 2012, Dustin Lundquist <dustin@null-ptr.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
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
