/*
 * Copyright (c) 2020 Cisco and/or its affiliates.
 *
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
/*
 * This is a minimal DTLS implementation intended only to parse the server name
 * extension.  This was created based primarily on Wireshark dissection of a
 * DTLS handshake and RFC4366.
 */

#include <stdio.h>
#include <stdlib.h> /* malloc() */
#include <stdint.h>
#include <string.h> /* strncpy() */
#include <sys/socket.h>
#include <sys/types.h>
#include "dtls.h"
#include "sni.h"
#include "protocol.h"
#include "logger.h"

#define DTLS_HANDSHAKE_CONTENT_TYPE      0x16
#define DTLS_VERSION_12_MAJOR            0xfe
#define DTLS_VERSION_12_MINOR            0xfd
#define DTLS_HEADER_LEN                  13
#define DTLS_HANDSHAKE_TYPE_CLIENT_HELLO 0x01

#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

static int parse_dtls_header(const uint8_t*, size_t, char **);

static const char dtls_alert[] = {
    0x15, /* DTLS Alert */
    0xfe, 0xfd, /* DTLS version  */
    0x00, 0x02, /* Payload length */
    0x02, 0x28, /* Fatal, handshake failure */
};

const struct Protocol *const dtls_protocol = &(struct Protocol){
    .name = "dtls",
    .default_port = 443,
    .parse_packet = (int (*const)(const char *, size_t, char **))&parse_dtls_header,
    .abort_message = dtls_alert,
    .abort_message_len = sizeof(dtls_alert)
};


/* Parse a DTLS packet for the Server Name Indication extension in the client
 * hello handshake, returning the first servername found (pointer to static
 * array)
 *
 * Returns:
 *  >=0  - length of the hostname and updates *hostname
 *         caller is responsible for freeing *hostname
 *  -1   - Incomplete request
 *  -2   - No Host header included in this request
 *  -3   - Invalid hostname pointer
 *  -4   - malloc failure
 *  < -4 - Invalid TLS client hello
 */
static int
parse_dtls_header(const uint8_t *data, size_t data_len, char **hostname) {
    uint8_t dtls_content_type;
    uint8_t dtls_version_major;
    uint8_t dtls_version_minor;
    size_t pos = DTLS_HEADER_LEN;
    size_t len;

    if (hostname == NULL)
        return -3;

    /* Check that our UDP payload is at least large enough for a DTLS header */
    if (data_len < DTLS_HEADER_LEN)
        return -1;


    dtls_content_type = data[0];
    if (dtls_content_type != DTLS_HANDSHAKE_CONTENT_TYPE) {
        debug("Request did not begin with DTLS handshake.");
        return -5;
    }

    dtls_version_major = data[1];
    dtls_version_minor = data[2];
    if (dtls_version_major != DTLS_VERSION_12_MAJOR &&
        dtls_version_minor != DTLS_VERSION_12_MINOR) {
        debug("Requested version of DTLS not supported");
        return -5;
    }

    /*
     * Skip epoch (2 bytes) and sequence number (6 bytes).
     * We want the length of this packet.
     */
    len = ((size_t)data[11] << 8) +
        (size_t)data[12] + DTLS_HEADER_LEN;
    data_len = MIN(data_len, len);

    /* Check we received entire DTLS record length */
    if (data_len < len) {
        debug("Failed to receive entire packet: len %zu data_len %zu", len, data_len);
        return -1;
    }

    /*
     * Handshake
     */
    if (pos + 1 > data_len) {
        debug("handshake");
        return -5;
    }
    if (data[pos] != DTLS_HANDSHAKE_TYPE_CLIENT_HELLO) {
        debug("Not a client hello");
        return -5;
    }

    /*
     * Skip past the following records:
     *
     * Length                  3
     * Message sequence        2
     * Fragment offset         3
     * Fragment length         3
     * Version                 2
     * Random                 32
     */
    pos += 46;

    /* Session ID */
    if (pos + 1 > data_len) {
        debug("Session ID incorrect");
        return -5;
    }
    len = (size_t)data[pos];
    debug("session ID length: 0x%zx", len);
    pos += 1 + len;

    /* Cookie length */
    pos += 1;

    /* Cipher Suites */
    if (pos + 2 > data_len) {
        debug("cipher suites");
        return -5;
    }
    len = ((size_t)data[pos] << 8) + (size_t)data[pos + 1];
    pos += 2 + len;

    /* Compression Methods */
    if (pos + 1 > data_len) {
        debug("compression methods");
        return -5;
    }
    len = (size_t)data[pos];
    pos += 1 + len;

    /* Extensions */
    if (pos + 2 > data_len) {
        printf("extensions");
        return -5;
    }
    len = ((size_t)data[pos] << 8) + (size_t)data[pos + 1];
    pos += 2;

    if (pos + len > data_len) {
        debug("length error");
        return -5;
    }
    return parse_extensions(data + pos, len, hostname);
}
