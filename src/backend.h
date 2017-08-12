/*
 * Copyright (c) 2011 and 2012, Dustin Lundquist <dustin@null-ptr.net>
 * Copyright (c) 2011 Manuel Kasper <mk@neon1.net>
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
#ifndef BACKEND_H
#define BACKEND_H

#include <sys/queue.h>
#include <pcre.h>
#include "address.h"

#ifndef STAILQ_INIT
/*
 * Singly-linked Tail queue declarations.
 */
#define STAILQ_HEAD(name, type)                                 \
struct name {                                                           \
        struct type *stqh_first;        /* first element */                     \
        struct type **stqh_last;        /* addr of last next element */         \
}

#define STAILQ_HEAD_INITIALIZER(head)                                   \
        { NULL, &(head).stqh_first }

#define STAILQ_ENTRY(type)                                              \
struct {                                                                \
        struct type *stqe_next; /* next element */                      \
}

/*
 * Singly-linked Tail queue functions.
 */
#define STAILQ_INIT(head) do {                                          \
        (head)->stqh_first = NULL;                                      \
        (head)->stqh_last = &(head)->stqh_first;                                \
} while (/*CONSTCOND*/0)

#define STAILQ_INSERT_HEAD(head, elm, field) do {                       \
        if (((elm)->field.stqe_next = (head)->stqh_first) == NULL)      \
                (head)->stqh_last = &(elm)->field.stqe_next;            \
        (head)->stqh_first = (elm);                                     \
} while (/*CONSTCOND*/0)

#define STAILQ_INSERT_TAIL(head, elm, field) do {                       \
        (elm)->field.stqe_next = NULL;                                  \
        *(head)->stqh_last = (elm);                                     \
        (head)->stqh_last = &(elm)->field.stqe_next;                    \
} while (/*CONSTCOND*/0)

#define STAILQ_INSERT_AFTER(head, listelm, elm, field) do {             \
        if (((elm)->field.stqe_next = (listelm)->field.stqe_next) == NULL)\
                (head)->stqh_last = &(elm)->field.stqe_next;            \
        (listelm)->field.stqe_next = (elm);                             \
} while (/*CONSTCOND*/0)

#define STAILQ_REMOVE_HEAD(head, field) do {                            \
        if (((head)->stqh_first = (head)->stqh_first->field.stqe_next) == NULL) \
                (head)->stqh_last = &(head)->stqh_first;                        \
} while (/*CONSTCOND*/0)

#define STAILQ_REMOVE(head, elm, type, field) do {                      \
        if ((head)->stqh_first == (elm)) {                              \
                STAILQ_REMOVE_HEAD((head), field);                      \
        } else {                                                        \
                struct type *curelm = (head)->stqh_first;               \
                while (curelm->field.stqe_next != (elm))                        \
                        curelm = curelm->field.stqe_next;               \
                if ((curelm->field.stqe_next =                          \
                        curelm->field.stqe_next->field.stqe_next) == NULL) \
                            (head)->stqh_last = &(curelm)->field.stqe_next; \
        }                                                               \
} while (/*CONSTCOND*/0)

#define STAILQ_FOREACH(var, head, field)                                \
        for ((var) = ((head)->stqh_first);                              \
                (var);                                                  \
                (var) = ((var)->field.stqe_next))

#define STAILQ_CONCAT(head1, head2) do {                                \
        if (!STAILQ_EMPTY((head2))) {                                   \
                *(head1)->stqh_last = (head2)->stqh_first;              \
                (head1)->stqh_last = (head2)->stqh_last;                \
                STAILQ_INIT((head2));                                   \
        }                                                               \
} while (/*CONSTCOND*/0)

/*
 * Singly-linked Tail queue access methods.
 */
#define STAILQ_EMPTY(head)      ((head)->stqh_first == NULL)
#define STAILQ_FIRST(head)      ((head)->stqh_first)
#define STAILQ_NEXT(elm, field) ((elm)->field.stqe_next)
#endif

STAILQ_HEAD(Backend_head, Backend);

struct Backend {
    char *pattern;
    struct Address *address;
    int use_proxy_header;

    /* Runtime fields */
    pcre *pattern_re;
    STAILQ_ENTRY(Backend) entries;
};

void add_backend(struct Backend_head *, struct Backend *);
int init_backend(struct Backend *);
struct Backend *lookup_backend(const struct Backend_head *, const char *, size_t);
void print_backend_config(FILE *, const struct Backend *);
void remove_backend(struct Backend_head *, struct Backend *);
struct Backend *new_backend();
int accept_backend_arg(struct Backend *, const char *);


#endif
