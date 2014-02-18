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
/*
 * This is a minimal TLS implementation intended only to parse the server name
 * extension.  This was created based primarily on Wireshark dissection of a
 * TLS handshake and RFC4366.
 */
#include <stdio.h>
#include <stdlib.h> /* malloc() */
#include <string.h> /* strncpy() */
#include <unistd.h> /* close() */
#include <sys/socket.h>
#include "tls.h"
#include "protocol.h"
#include "logger.h"

#define SERVER_NAME_LEN 256
#define TLS_HEADER_LEN 5
#define TLS_HANDSHAKE_CONTENT_TYPE 0x16
#define TLS_HANDSHAKE_TYPE_CLIENT_HELLO 0x01

#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

static const char tls_alert[] = {
    0x15, /* TLS Alert */
    0x03, 0x01, /* TLS version  */
    0x00, 0x02, /* Payload length */
    0x02, 0x28, /* Fatal, handshake failure */
};

static int parse_tls_header(const struct Listener *, const char*, size_t, struct ProtocolRes*);
static int parse_extensions(const struct Listener *, const char *, size_t, struct ProtocolRes*);
static int parse_server_name_extension(const struct Listener *, const char *, size_t, struct ProtocolRes*);
static int
parse_alpn_extension(const struct Listener * t, const char *data, size_t data_len,
                     struct ProtocolRes* pres);

static const struct Protocol tls_protocol_st = {
    .name = "tls",
    .default_port = 443,
    .parse_packet = &parse_tls_header,
    .abort_message = tls_alert,
    .abort_message_len = sizeof(tls_alert)
};
const struct Protocol *tls_protocol = &tls_protocol_st;


/* Parse a TLS packet for the Server Name Indication extension in the client
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
parse_tls_header(const struct Listener * t, const char* data, size_t data_len,
                 struct ProtocolRes* pres)
{
    char tls_content_type;
    char tls_version_major;
    char tls_version_minor;
    size_t pos = TLS_HEADER_LEN;
    size_t len;

    if (pres == NULL)
        return -3;

    /* Check that our TCP payload is at least large enough for a TLS header */
    if (data_len < TLS_HEADER_LEN)
        return -1;

    tls_content_type = data[0];
    if (tls_content_type != TLS_HANDSHAKE_CONTENT_TYPE) {
        debug("Request did not begin with TLS handshake.");
        return -5;
    }

    tls_version_major = data[1];
    tls_version_minor = data[2];
    if (tls_version_major < 3) {
        debug("Received SSL %d.%d handshake which cannot be parsed.",
              tls_version_major, tls_version_minor);

        return -2;
    }

    /* TLS record length */
    len = ((unsigned char)data[3] << 8) +
        (unsigned char)data[4] + TLS_HEADER_LEN;
    data_len = MIN(data_len, len);

    /* Check we received entire TLS record length */
    if (data_len < len)
        return -1;

    /*
     * Handshake
     */
    if (pos + 1 > data_len) {
        return -5;
    }
    if (data[pos] != TLS_HANDSHAKE_TYPE_CLIENT_HELLO) {
        debug("Not a client hello");

        return -5;
    }

    /* Skip past fixed length records:
       1	Handshake Type
       3	Length
       2	Version (again)
       32	Random
       to	Session ID Length
     */
    pos += 38;

    /* Session ID */
    if (pos + 1 > data_len)
        return -5;
    len = (unsigned char)data[pos];
    pos += 1 + len;

    /* Cipher Suites */
    if (pos + 2 > data_len)
        return -5;
    len = ((unsigned char)data[pos] << 8) + (unsigned char)data[pos + 1];
    pos += 2 + len;

    /* Compression Methods */
    if (pos + 1 > data_len)
        return -5;
    len = (unsigned char)data[pos];
    pos += 1 + len;

    /* Extensions */
    if (pos + 2 > data_len)
        return -5;
    len = ((unsigned char)data[pos] << 8) + (unsigned char)data[pos + 1];
    pos += 2;

    if (pos + len > data_len)
        return -5;
    return parse_extensions(t, data + pos, len, pres);
}

static int
parse_extensions(const struct Listener * l, const char *data, size_t data_len,
                 struct ProtocolRes* pres) {
    size_t pos = 0;
    size_t sn_pos, alpn_pos;
    size_t len, alpn_len, sn_len;
    int ret;

    sn_pos = alpn_pos = 0;
    alpn_len = sn_len = 0;

    /* Parse each 4 bytes for the extension header */
    while (pos + 4 < data_len) {
        /* Extension Length */
        len = ((unsigned char)data[pos + 2] << 8) +
            (unsigned char)data[pos + 3];

        /* Check if it's a server name extension */
        if (data[pos] == 0x00) {
            if (data[pos + 1] == 0x00) { /* server name */
                /* There can be only one extension of each type, so we break
                   our state and move p to beinnging of the extension here */
                if (pos + 4 + len > data_len)
                    return -5;

                sn_pos = pos + 4;
                sn_len = len;
            } else if (data[pos + 1] == 0x10) { /* ALPN */
                if (pos + 4 + len > data_len)
                    return -5;

                alpn_pos = pos + 4;
                alpn_len = len;
            }
        }
        pos += 4 + len; /* Advance to the next extension header */
    }

    /* Check we ended where we expected to */
    if (pos != data_len)
        return -5;

    if ((l->prefer_alpn && alpn_pos != 0) || (sn_pos == 0 && alpn_pos != 0)) {
        ret = parse_alpn_extension(l, data + alpn_pos, alpn_len, pres);
	/* if we fail allow fall back to SNI */
	if (ret >= 0)
		return ret;
    }

    if (sn_pos != 0) {
        return parse_server_name_extension(l, data + sn_pos, sn_len, pres);
    }

    return -2;
}

static int
parse_server_name_extension(const struct Listener * l, const char *data, size_t data_len,
                            struct ProtocolRes* pres) {
    size_t pos = 2; /* skip server name list length */
    size_t len;
    char* hostname;

    while (pos + 3 < data_len) {
        len = ((unsigned char)data[pos + 1] << 8) +
            (unsigned char)data[pos + 2];

        if (pos + 3 + len > data_len)
            return -5;

        switch (data[pos]) { /* name type */
            case 0x00: /* host_name */
                hostname = malloc(len + 1);
                if (hostname == NULL) {
                    err("malloc() failure");
                    return -4;
                }

                strncpy(hostname, data + pos + 3, len);

                hostname[len] = '\0';

                pres->name = hostname;
                pres->name_size = len;
                pres->name_type = NTYPE_HOST;

                return len;
            default:
                debug("Unknown server name extension name type: %d",
                      data[pos]);
        }
        pos += 3 + len;
    }
    /* Check we ended where we expected to */
    if (pos != data_len)
        return -5;

    return -2;
}

static int is_alpn_proto_known(const struct Listener * l, const char* name, unsigned name_size)
{
#ifdef SELF_CONTAINED
	return 1;
#else
	if (table_lookup_backend(l->alpn_table, name, name_size) != NULL)
		return 1;
	return 0;
#endif
}

static int
parse_alpn_extension(const struct Listener * l, const char *data, size_t data_len,
                     struct ProtocolRes* pres) {
    size_t pos = 2;
    size_t len;
    char* hostname;

    while (pos + 1 < data_len) {
        len = (unsigned char)data[pos];

        if (pos + 1 + len > data_len)
            return -5;

        if (len > 0 && is_alpn_proto_known(l, data + pos + 1, len)) {
	    hostname = malloc(len + 1);
            if (hostname == NULL) {
                err("malloc() failure");
                return -4;
            }

            memcpy(hostname, data + pos + 1, len);
            hostname[len] = '\0';

            pres->name = hostname;
            pres->name_size = len;
            pres->name_type = NTYPE_ALPN;

	    return len;
	} else if (len > 0) {
            debug("Unknown ALPN name: %.*s", (int)len, data + pos + 2);
        }
        pos += 1 + len;
    }
    /* Check we ended where we expected to */
    if (pos != data_len)
        return -5;

    return -2;
}
