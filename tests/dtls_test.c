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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "dtls.h"

struct test_packet {
    const char *packet;
    size_t len;
    const char *hostname;
};

const char good_hostname_1[] = "nginx1.umbrella.com";
const unsigned char good_data_1[] = {
    // UDP payload length
    0x00, 0xdd,
    // DTLS Record Layer
    0x16, // Content Type: Handshake
    0xfe, 0xfd, // Version: DTLS 1.2
    0x00, 0x00, // Epoch
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Sequence Number
    0x00, 0xc8, // Length
        // Handshake
        0x01, // Handshake Type: Client Hello
        0x00, 0x00, 0xbc, // Length
        0x00, 0x00, // Message sequence
        0x00, 0x00, 0x00, // Fragment offset
        0x00, 0x00, 0xbc, // Fragment length
        0xfe, 0xfd, // Version: DTLS 1.2
        0x4a, 0x72, 0xfb, 0x78, // Unix Time
        0xbc, 0x96, // Random
              0x1e, 0xf3, 0x78, 0x01, 0xa3, 0xa8,
              0xcf, 0x84, 0x14, 0xe5, 0xec, 0x06,
              0xee, 0xdb, 0x09, 0xde, 0x27, 0x62,
              0x3c, 0xd2, 0xb8, 0x00, 0x5f, 0x14,
              0x8c, 0xfc,
        0x20, // Session ID Length
              0x51, 0x1b, 0xb8, 0xf9, 0x10, 0x79, // Session ID
              0x23, 0x2c, 0xbb, 0x88, 0x92, 0x7c,
              0xb8, 0x51, 0x70, 0x8e, 0x50, 0x02,
              0x25, 0xd9, 0x85, 0xf3, 0x49, 0xe3,
              0xdb, 0x63, 0xf7, 0x4a, 0x06, 0xcb,
              0x6a, 0x0e,
        0x00, // Cookie Length
        0x00, 0x02, // Cipher suite length
        0xc0, 0x2c, // Cipher suite
        0x01, // Compression method length
        0x00, // Compression method
        0x00, 0x70, // Extension length
              0x00, 0x00, // Sever Name
              0x00, 0x18, // Length
              0x00, 0x16, // Server Name List Length
                    0x00, // Server name type: hostname
                    0x00, 0x13, // Server name length
                    0x6e, 0x67, 0x69, 0x6e, // Server name: nginx1.umbrella.com
                    0x78, 0x31, 0x2e, 0x75,
                    0x6d, 0x62, 0x72, 0x65,
                    0x6c, 0x6c, 0x61, 0x2e,
                    0x63, 0x6f, 0x6d,
              0x00, 0x0b, // Type: ec_point_formats
              0x00, 0x04, // Length
                    0x03, // EC point format length
                    0x00, // Uncompressed
                    0x01, // ansiX962_compressed_prime
                    0x02, // ansiX962_compressed_char2
              0x00, 0x0a, // Type: Supported groups
                    0x00, 0x0c, // Length
                    0x00, 0x0a, // Supported groups list length
                          0x00, 0x1d, // x25519
                          0x00, 0x17, // secp256r1
                          0x00, 0x1e, // x448
                          0x00, 0x19, // secp521r1
                          0x00, 0x18, // secp384r1
              0x00, 0x16, // Type: encrypt_then_mac
                    0x00, 0x00, // Length
              0x00, 0x17, // Type: extended_master_secret
                    0x00, 0x00, // Length
              0x00, 0x0d, // Type: Signature algorithms
                    0x00, 0x30, // Length
                    0x00, 0x2e, // Hash Algorithms length
                          // ecdsa_secp256r1_sha256
                          0x04, // SHA256
                          0x03, // EDCSA
                          // ecdsa_secp384r1_sha384
                          0x05, // SHA384
                          0x03, // EDCSA
                          // ecdsa_secp521r1_sha512
                          0x06, // SHA512
                          0x03, // EDCSA
                          // ed25519
                          0x08, // unknown
                          0x07, // unknown
                          // ed448
                          0x08, // unknown
                          0x08, // unknown
                          // rsa_pss_pss_sha256
                          0x08, // unknown
                          0x09, // unknown
                          // rsa_pss_pss_sha384
                          0x08, // unknown
                          0x0a, // unknown
                          // rsa_pss_pss_sha512
                          0x08, // unknown
                          0x0b, // unknown
                          // rsa_pss_pss_sha256
                          0x08, // unknown
                          0x04, // unknown
                          // rsa_pss_rsae_sha384
                          0x08, // unknown
                          0x05, // unknown
                          // rsa_pss_rsae_sha512
                          0x08, // unknown
                          0x06, // unknown
                          // rsa_pkcs1_sha256
                          0x04, // SHA256
                          0x01, // RSA
                          // rsa_pkcs1_sha384
                          0x05, // SHA384
                          0x01, // RSA
                          // rsa_pkcs1_sha512
                          0x06, // SHA512
                          0x01, // RSA
                          // SHA224 EDCSA
                          0x03, // SHA224
                          0x03, // EDCSA 
                          // edcsa_sha1
                          0x02, // SHA1
                          0x03, // EDCSA
                          // SHA224 RSA
                          0x03, // SHA224
                          0x01, // RSA
                          // rsa_pkcs1_sha1
                          0x02, // SHA1
                          0x01, // RSA
                          // SHA224 DSA
                          0x03, // SHA224
                          0x02, // DSA
                          // SHA1 DSA
                          0x02, // SHA1
                          0x02, // DSA
                          // SHA256 DSA
                          0x04, // SHA256
                          0x02, // DSA
                          // SHA384 DSA
                          0x05, // SHA384
                          0x02, // DSA
                          // SHA512 DSA
                          0x06, // SHA512
                          0x02 // DSA
};

const unsigned char bad_data_1[] = {
    0x16, 0x03, 0x01, 0x00, 0x68, 0x01, 0x00, 0x00,
    0x64, 0x03, 0x01, 0x4e, 0x4e, 0xbe, 0xc2, 0xa1,
    0x21, 0xad, 0xbc, 0x28, 0x33, 0xca, 0xa1, 0xd6,
    0x6e, 0x57, 0xb9, 0x1f, 0x8c, 0x19, 0x0e, 0x44,
    0x16, 0x9e, 0x7d, 0x20, 0x35, 0x4b, 0x65, 0xb2,
    0xc0, 0xd5, 0xa8, 0x00, 0x00, 0x28, 0x00, 0x39,
    0x00, 0x38, 0x00, 0x35, 0x00, 0x16, 0x00, 0x13,
    0x00, 0x0a, 0x00, 0x33, 0x00, 0x32, 0x00, 0x2f,
    0x00, 0x05, 0x00, 0x04, 0x00, 0x15, 0x00, 0x12,
    0x00, 0x09, 0x00, 0x14, 0x00, 0x11, 0x00, 0x08,
    0x00, 0x06, 0x00, 0x03, 0x00, 0xff, 0x02, 0x01,
    0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x0e, 0x00,
    0x0c, 0x00, 0x00, 0x09, 0x6c, 0x6f, 0x64, 0x61,
    0x6c, 0x68, 0x6f, 0x73, 0x74
};

const unsigned char bad_data_2[] = {
    0x16, 0x03, 0x01, 0x00, 0x68, 0x01, 0x00, 0x00,
    0x64, 0x03, 0x01, 0x4e, 0x4e, 0xbe, 0xc2, 0xa1,
    0x21, 0xad, 0xbc, 0x28, 0x33, 0xca, 0xa1, 0xd6,
    0x6e, 0x57, 0xb9, 0x1f, 0x8c, 0x19, 0x0e, 0x44,
    0x16, 0x9e, 0x7d, 0x20, 0x35, 0x4b, 0x65, 0xb2,
    0xc0, 0xd5, 0xa8, 0x00, 0x00, 0x28, 0x00, 0x39,
    0x00, 0x38, 0x00, 0x35, 0x00, 0x16, 0x00, 0x13,
    0x00, 0x0a, 0x00, 0x33, 0x00, 0x32, 0x00, 0x2f,
    0x00, 0x05, 0x00, 0x04, 0x00, 0x15, 0x00, 0x12,
    0x00, 0x09, 0x00, 0x14, 0x00, 0x11, 0x00, 0x08,
    0x00, 0x06, 0x00, 0x03, 0x00, 0xff, 0x02, 0x01,
    0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x0e, 0x00
};

const unsigned char bad_data_3[] = {
    0x16, 0x03, 0x01, 0x00
};

static struct test_packet good[] = {
    { (char *)good_data_1, sizeof(good_data_1), good_hostname_1 },
};

static struct test_packet bad[] = {
    { (char *)bad_data_1, sizeof(bad_data_1), NULL },
    { (char *)bad_data_2, sizeof(bad_data_2), NULL },
    { (char *)bad_data_3, sizeof(bad_data_3), NULL }
};

int main() {
    unsigned int i;
    int result;
    char *hostname;

    for (i = 0; i < sizeof(good) / sizeof(struct test_packet); i++) {
        hostname = NULL;

        printf("Testing packet of length %zu\n", good[i].len);
        result = dtls_protocol->parse_packet(good[i].packet, good[i].len, &hostname);

        assert(result == 19);

        assert(NULL != hostname);

        assert(0 == strcmp(good[i].hostname, hostname));

        free(hostname);
    }

    result = dtls_protocol->parse_packet(good[0].packet, good[0].len, NULL);
    assert(result == -3);

    for (i = 0; i < sizeof(bad) / sizeof(struct test_packet); i++) {
        hostname = NULL;

        result = dtls_protocol->parse_packet(bad[i].packet, bad[i].len, &hostname);

        // parse failure or not "localhost"
        if (bad[i].hostname != NULL)
            assert(result < 0 ||
                   hostname == NULL ||
                   strcmp(bad[i].hostname, hostname) != 0);

        free(hostname);
    }

    return 0;
}

