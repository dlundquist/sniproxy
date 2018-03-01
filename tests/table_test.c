#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "table.h"
#include "backend.h"


static void test_empty_table();
static void test_single_entry_table();
static void append_entry(struct Table *, const char *, const char *);
static void add_new_table(struct Table_head *, const char *, const char **);
static void test_add_table();
static void test_tables_reload();
static int count_tables(const struct Table_head *);


int main() {
    test_empty_table();
    test_single_entry_table();
    test_add_table();
    test_tables_reload();
}

static void
test_empty_table() {
    struct Table *table = new_table();
    assert(table != NULL);

    table_ref_get(table);

    const char *name = "table_name";
    accept_table_arg(table, name);
    assert(table->name != NULL);
    assert(strcmp(name, table->name) == 0);
    assert(name != table->name);

    const char *server_query = "example.com";
    assert(table_lookup_server_address(table, server_query,
            strlen(server_query)) == NULL);

    table_ref_put(table);
}

static void
append_entry(struct Table *table, const char *pattern, const char *address) {
    struct Backend *backend = new_backend();
    assert(backend != NULL);
    accept_backend_arg(backend, pattern);
    accept_backend_arg(backend, address);
    assert(strcmp(pattern, backend->pattern) == 0);
    assert(pattern != backend->pattern);

    add_backend(&table->backends, backend);
}

static void
test_single_entry_table() {
    struct Table *table = new_table();
    assert(table != NULL);

    table_ref_get(table);

    const char *name = "table_name";
    accept_table_arg(table, name);
    assert(table->name != NULL);
    assert(strcmp(name, table->name) == 0);
    assert(name != table->name);

    append_entry(table, "^example\\.com$", "192.0.2.10");

    init_table(table);

    const char *server_query = "example.com";
    assert(table_lookup_server_address(table, server_query,
            strlen(server_query)) != NULL);

    table_ref_put(table);
}

static void
add_new_table(struct Table_head *tables, const char *name, const char **entries) {
    struct Table *table = new_table();
    assert(table != NULL);

    accept_table_arg(table, name);
    assert(table->name != NULL);
    assert(table->name != name);
    assert(strcmp(table->name, name) == 0);

    while (entries != NULL && entries[0] != NULL && entries[1] != NULL) {
        append_entry(table, entries[0], entries[1]);
        entries += 2;
    }

    add_table(tables, table);
}

static int
count_tables(const struct Table_head *tables) {
    struct Table *iter;
    int count = 0;

    SLIST_FOREACH(iter, tables, entries)
        count++;

    return count;
}

static void
test_add_table() {
    struct Table_head tables = SLIST_HEAD_INITIALIZER();

    add_new_table(&tables, "foo", (const char *[]){
            "^example\\.com$", "192.0.2.10",
            "^.*$", "*",
            NULL});
    add_new_table(&tables, "bar", (const char *[]){
            "^example\\.net$", "192.0.2.11",
            NULL});
    add_new_table(&tables, "baz", (const char *[]){
            "^example\\.org$", "192.0.2.12",
            NULL});

    struct Table *result = table_lookup(&tables, "baz");
    assert(result != NULL);
    assert(strcmp("baz", result->name) == 0);

    result = table_lookup(&tables, "foo");
    assert(result != NULL);
    assert(strcmp("foo", result->name) == 0);

    result = table_lookup(&tables, "bar");
    assert(result != NULL);
    assert(strcmp("bar", result->name) == 0);

    result = table_lookup(&tables, "qvf");
    assert(result == NULL);

    assert(count_tables(&tables) == 3);

    free_tables(&tables);
}

static void
test_tables_reload() {
    struct Table_head existing = SLIST_HEAD_INITIALIZER();
    struct Table_head new = SLIST_HEAD_INITIALIZER();
    struct Table *table = NULL;

    add_new_table(&existing, "foo", (const char *[]){
            "^example\\.com$", "192.0.2.10",
            "^.*$", "*",
            NULL});
    add_new_table(&existing, "bar", (const char *[]){
            "^example\\.net$", "192.0.2.11",
            NULL});

    add_new_table(&new, "baz", (const char *[]){
            "^example\\.org$", "192.0.2.12",
            NULL});
    add_new_table(&new, "foo", (const char *[]){
            "^.*$", "*",
            NULL});

    struct Table *bar = table_lookup(&existing, "bar");
    assert(bar != NULL);
    table_ref_get(bar);

    reload_tables(&existing, &new);

    assert(count_tables(&new) == 0);
    free_tables(&new);

    assert(count_tables(&existing) == 2);

    table = table_lookup(&existing, "baz");
    assert(table != NULL);
    assert(strcmp("baz", table->name) == 0);

    table = table_lookup(&existing, "foo");
    assert(table != NULL);
    assert(strcmp("foo", table->name) == 0);

    free_tables(&existing);
    table_ref_put(bar);
}
