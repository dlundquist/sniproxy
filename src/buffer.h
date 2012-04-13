#ifndef BUFFER_H 
#define BUFFER_H

#include <stdio.h>

struct Buffer {
    char *buffer;
    size_t size;
    size_t head;
    size_t len;
};

struct Buffer *new_buffer();
void free_buffer(struct Buffer *);

ssize_t buffer_recv(struct Buffer *, int, int);
ssize_t buffer_send(struct Buffer *, int, int);
ssize_t buffer_read(struct Buffer *, int);
ssize_t buffer_write(struct Buffer *, int);
size_t buffer_peek(const struct Buffer *, void *, size_t);
size_t buffer_pop(struct Buffer *, void *, size_t);
size_t buffer_push(struct Buffer *, const void *, size_t);
static inline size_t buffer_len(const struct Buffer *b) {
    return b->len;
}
static inline size_t buffer_room(const struct Buffer *b) {
    return b->size - b->len;
}

#endif
