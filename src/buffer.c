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
#include <stdio.h>
#include <stdlib.h> /* malloc */
#include <string.h> /* memcpy */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include "buffer.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))


static void parse_ancillary_data(struct msghdr *, struct timeval *);
static size_t setup_write_iov(const struct Buffer *, struct iovec *, size_t);
static size_t setup_read_iov(const struct Buffer *, struct iovec *, size_t);
static inline void advance_write_position(struct Buffer *, size_t);
static inline void advance_read_position(struct Buffer *, size_t);


struct Buffer *
new_buffer(int size) {
    struct Buffer *buf;

    buf = malloc(sizeof(struct Buffer));
    if (buf == NULL) {
        return NULL;
    }

    buf->size = size;
    buf->len = 0;
    buf->head = 0;
    buf->tx_bytes = 0;
    buf->rx_bytes = 0;
    buf->buffer = malloc(buf->size);
    if (buf->buffer == NULL) {
        free(buf);
        buf = NULL;
    }

    return buf;
}

ssize_t
buffer_resize(struct Buffer *buf, size_t new_size) {
    char *new_buffer;

    if (new_size < buf->len)
        return -1; /* new_size too small to hold existing data */

    new_buffer = malloc(new_size);
    if (new_buffer == NULL)
        return -2;

    if (buffer_peek(buf, new_buffer, new_size) != buf->len) {
        /* failed to copy all that data to the new buffer */
        free(new_buffer);
        return -3;
    }

    free(buf->buffer);
    buf->buffer = new_buffer;
    buf->head = 0;

    return buf->len;
}

void
free_buffer(struct Buffer *buf) {
    if (buf == NULL)
        return;

    free(buf->buffer);
    free(buf);
}

ssize_t
buffer_recv(struct Buffer *buffer, int sockfd, int flags) {
    ssize_t bytes;
    struct iovec iov[2];
    struct msghdr msg;
    char control_buf[64];

    memset(control_buf, 0, sizeof(control_buf));

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = setup_write_iov(buffer, iov, 0);
    msg.msg_control = (void *)control_buf;
    msg.msg_controllen = sizeof(control_buf);
    msg.msg_flags = 0;

    bytes = recvmsg(sockfd, &msg, flags);

    parse_ancillary_data(&msg, &buffer->last_recv);

    if (bytes > 0)
        advance_write_position(buffer, bytes);

    return bytes;
}

ssize_t
buffer_send(struct Buffer *buffer, int sockfd, int flags) {
    ssize_t bytes;
    struct iovec iov[2];
    char control_buf[64];
    struct msghdr msg;

    memset(control_buf, 0, sizeof(control_buf));

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = setup_read_iov(buffer, iov, 0);
    msg.msg_control = (void *)control_buf;
    msg.msg_controllen = sizeof(control_buf);
    msg.msg_flags = 0;

    bytes = sendmsg(sockfd, &msg, flags);

    parse_ancillary_data(&msg, &buffer->last_send);

    if (bytes > 0)
        advance_read_position(buffer, bytes);

    return bytes;
}

/*
 * Read data from file into buffer
 */
ssize_t
buffer_read(struct Buffer *buffer, int fd) {
    ssize_t bytes;
    struct iovec iov[2];

    bytes = readv(fd, iov, setup_write_iov(buffer, iov, 0));

    if (bytes > 0)
        advance_write_position(buffer, bytes);

    return bytes;
}

/*
 * Write data to file from buffer
 */
ssize_t
buffer_write(struct Buffer *buffer, int fd) {
    ssize_t bytes;
    struct iovec iov[2];

    bytes = writev(fd, iov, setup_read_iov(buffer, iov, 0));

    if (bytes > 0)
        advance_read_position(buffer, bytes);

    return bytes;
}

size_t
buffer_peek(const struct Buffer *src, void *dst, size_t len) {
    struct iovec iov[2];
    int iov_len;
    int i;
    size_t bytes_copied = 0;

    iov_len = setup_read_iov(src, iov, len);

    for (i = 0; i < iov_len; i ++) {
        memcpy((char *)dst + bytes_copied, iov[i].iov_base, iov[i].iov_len);

        bytes_copied += iov[i].iov_len;
    }

    return bytes_copied;
}

size_t
buffer_pop(struct Buffer *src, void *dst, size_t len) {
    size_t bytes;

    bytes = buffer_peek(src, dst, len);

    if (bytes > 0)
        advance_read_position(src, bytes);

    return bytes;
}

size_t
buffer_push(struct Buffer *dst, const void *src, size_t len) {
    struct iovec iov[2];
    int iov_len;
    int i;
    size_t bytes_appended = 0;

    if (dst->size - dst->len < len)
        return -1; /* insufficient room */

    iov_len = setup_write_iov(dst, iov, len);

    for (i = 0; i < iov_len; i++) {
        memcpy(iov[i].iov_base, (char *)src + bytes_appended, iov[i].iov_len);
        bytes_appended += iov[i].iov_len;
    }

    if (bytes_appended > 0)
        advance_write_position(dst, bytes_appended);

    return bytes_appended;
}

static void
parse_ancillary_data(struct msghdr *m, struct timeval *tv) {
    struct cmsghdr *cmsg;

    for (cmsg = CMSG_FIRSTHDR(m); cmsg != NULL; cmsg = CMSG_NXTHDR(m, cmsg))
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMP)
            memcpy(tv, CMSG_DATA(cmsg), sizeof(struct timeval));
}

/*
 * Setup a struct iovec iov[2] for a write to a buffer.
 * struct iovec *iov MUST be at least length 2.
 * returns the number of entries setup
 */
static size_t
setup_write_iov(const struct Buffer *buffer, struct iovec *iov, size_t len) {
    size_t room;
    size_t start;
    size_t end;

    room = buffer->size - buffer->len;
    if (room == 0) /* trivial case: no room */
        return 0;

    /* Allow caller to specify maximum length */
    if (len)
        room = MIN(room, len);

    start = (buffer->head + buffer->len) % buffer->size;
    end = (start + room) % buffer->size;

    if (end > start) { /* simple case */
        iov[0].iov_base = &buffer->buffer[start];
        iov[0].iov_len = room;

        return 1;
    } else { /* wrap around case */
        iov[0].iov_base = &buffer->buffer[start];
        iov[0].iov_len = buffer->size - start;
        iov[1].iov_base = buffer->buffer;
        iov[1].iov_len = room - iov[0].iov_len;

        return 2;
    }
}

static size_t
setup_read_iov(const struct Buffer *buffer, struct iovec *iov, size_t len) {
    size_t end;

    if (buffer->len == 0)
        return 0;

    end = buffer->head + buffer->len;

    if (len)
        end = MIN(end, buffer->head + len);

    end = end % buffer->size;

    if (end > buffer->head) {
        iov[0].iov_base = &buffer->buffer[buffer->head];
        iov[0].iov_len = end - buffer->head;

        return 1;
    } else {
        iov[0].iov_base = &buffer->buffer[buffer->head];
        iov[0].iov_len = buffer->size - buffer->head;
        iov[1].iov_base = buffer->buffer;
        iov[1].iov_len = end;

        return 2;
    }
}

static inline void
advance_write_position(struct Buffer *buffer, size_t offset) {
    buffer->len += offset;
    buffer->rx_bytes += offset;
}

static inline void
advance_read_position(struct Buffer *buffer, size_t offset) {
    buffer->head = (buffer->head + offset) % buffer->size;
    buffer->len -= offset;
    buffer->tx_bytes += offset;
}
