/*
 * Copyright (c) 2012, Dustin Lundquist <dustin@null-ptr.net>
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
#ifndef BUFFER_H
#define BUFFER_H

#include <stdio.h>
#include <sys/types.h>
#include <ev.h>


struct Buffer {
    char *buffer;
    size_t size_mask;       /* bit mask for buffer size */
    size_t head;            /* index of first byte of content */
    size_t len;             /* size of content */
    ev_tstamp last_recv;
    ev_tstamp last_send;
    size_t tx_bytes;
    size_t rx_bytes;
};

struct Buffer *new_buffer(size_t, struct ev_loop *);
void free_buffer(struct Buffer *);

ssize_t buffer_recv(struct Buffer *, int, int, struct ev_loop *);
ssize_t buffer_send(struct Buffer *, int, int, struct ev_loop *);
ssize_t buffer_read(struct Buffer *, int);
ssize_t buffer_write(struct Buffer *, int);
ssize_t buffer_resize(struct Buffer *, size_t);
size_t buffer_peek(const struct Buffer *, void *, size_t);
size_t buffer_coalesce(struct Buffer *, const void **);
size_t buffer_pop(struct Buffer *, void *, size_t);
size_t buffer_push(struct Buffer *, const void *, size_t);
static inline size_t buffer_size(const struct Buffer *b) {
    return b->size_mask + 1;
}
static inline size_t buffer_len(const struct Buffer *b) {
    return b->len;
}
static inline size_t buffer_room(const struct Buffer *b) {
    return buffer_size(b) - b->len;
}

#endif
