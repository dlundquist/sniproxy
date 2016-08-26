/*
 * Copyright (c) 2013, Dustin Lundquist <dustin@null-ptr.net>
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
#ifndef ADDRESS_H
#define ADDRESS_H

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>

/*
 * Define size of address buffers for display_address() calls to
 * be large enough for the maximum domain name and a 5 digit port
 */
#define ADDRESS_BUFFER_SIZE 262

struct Address;

struct Address *new_address(const char *);
struct Address *new_address_sa(const struct sockaddr *, socklen_t);
struct Address *copy_address(const struct Address *);
size_t address_len(const struct Address *);
int address_compare(const struct Address *, const struct Address *);
int address_is_hostname(const struct Address *);
int address_is_sockaddr(const struct Address *);
int address_is_wildcard(const struct Address *);
const char *address_hostname(const struct Address *);
const struct sockaddr *address_sa(const struct Address *);
socklen_t address_sa_len(const struct Address *);
uint16_t address_port(const struct Address *);
void address_set_port(struct Address *, uint16_t);
int address_set_port_str(struct Address *addr, const char* str);
const char *display_address(const struct Address *, char *, size_t);
const char *display_sockaddr(const void *, char *, size_t);
int is_numeric(const char *);

#endif
