#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h> /* bzero() */
#include <unistd.h> /* close() */
#include <ctype.h> /* isprint() in hexdump */
#include <string.h> /* strncpy() */


#define HTTPS_PORT 4343
#define BACKLOG 5
#define BUFFER_LEN 512
#define SERVER_NAME_LEN 256

void handle (int);
const char *parse_tls_header(const uint8_t *, int);
const char *parse_server_name_extension(const uint8_t *, int);
void hexdump(const void *, int);

int
main() {
	int sockfd, clientsockfd;
	unsigned int clilen;
	struct sockaddr_in serv_addr, cli_addr;


	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) {
		perror("ERROR opening socket");
		return -1;
	}

	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(HTTPS_PORT);

	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		perror("ERROR on binding");
		return (-1);
	}

	listen(sockfd, BACKLOG);

	clilen = sizeof(cli_addr);
	while(1) {
		clientsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		if (clientsockfd < 0)  {
			perror("ERROR on accept");
			return (-1);
		}

		handle(clientsockfd);
	}
}

const uint8_t tls_alert[] = {
	0x15, /* TLS Alert */
	0x03, 0x01, /* TLS version  */
	0x00, 0x02, /* Payload length */
	0x02, 0x28, /* Fatal, handshake failure */
};

void
handle (int sockfd) {
	char buffer[BUFFER_LEN];
	int n;
	const char* hostname;

	bzero(buffer, BUFFER_LEN);
	n = read(sockfd, buffer, BUFFER_LEN);
	if (n < 0) {
		perror("ERROR reading from socket");
		return;
	}

	hostname = parse_tls_header((uint8_t *)buffer, n);
	if (hostname == NULL) {
		/* send an alert and close the socket */
		/* TODO is there a better way to notify the client w/ need SNI? */
		send(sockfd, tls_alert, 7, 0);
		close(sockfd);

		fprintf(stderr, "Request did not include a hostname\n");
		hexdump(buffer, n);
		return;
	}


	fprintf(stderr, "Request for %s\n", hostname);
	close(sockfd);
}


const char *
parse_tls_header(const uint8_t* data, int data_len) {
	uint8_t tls_content_type;
	uint8_t tls_version_major;
	uint8_t tls_version_minor;
	uint16_t tls_length;
	const uint8_t* p = data;
	int len;

	/* Check that our TCP payload is atleast large enough for a TLS header */
	if (data_len < 5)
		return NULL;

	tls_content_type = p[0];
	tls_version_major = p[1];
	tls_version_minor = p[2];
	tls_length = (p[3] << 8) + p[4];

	if (tls_content_type != 0x16) {
		fprintf(stderr, "Did not receive TLS handshake\n");
		return NULL;
	}

	if (tls_version_major < 3) {
		fprintf(stderr, "Receved pre SSL 3.0 handshake\n\n");
		return NULL;
	}

	if (tls_version_major == 3 && tls_version_minor < 1) {
		fprintf(stderr, "Receved SSL 3.0 handshake\n\n");
		return NULL;
	}

	if (data_len < tls_length + 5) {
		fprintf(stderr, "Did not receive complete TLS handshake header: %d\n", __LINE__);
		return NULL;
	}



	/* Advance to first TLS payload */
	p += 5;

	if (p - data >= data_len) {
		fprintf(stderr, "Did not receive complete TLS handshake header: %d\n", __LINE__);
		return NULL;
	}

	if (*p != 0x01) { /* Client Hello */
		fprintf(stderr, "Not a client hello\n");
		return NULL;

	}

	/* Skip past:
	   1	Handshake Type
	   3	Length
	   2	Version (again)
	   32	Random
	   to	Session ID Length
	 */
	p += 38;
	if (p - data >= data_len) {
		fprintf(stderr, "Did not receive complete TLS handshake header: %d\n", __LINE__);
		return NULL;
	}

	len = *p; /* Session ID Length */
	p += 1 + len; /* Skip session ID block */
	if (p - data >= data_len) {
		fprintf(stderr, "Did not receive complete TLS handshake header: %d\n", __LINE__);
		return NULL;
	}

	len = *p << 8; /* Cipher Suites length high byte */
	p ++;
	if (p - data >= data_len) {
		fprintf(stderr, "Did not receive complete TLS handshake header: %d\n", __LINE__);
		return NULL;
	}
	len += *p; /* Cipher Suites length low byte */

	p += 1 + len;

	if (p - data >= data_len) {
		fprintf(stderr, "Did not receive complete TLS handshake header: %d\n", __LINE__);
		return NULL;
	}
	len = *p; /* Compression Methods length */

	p += 1 + len;


	if (p - data >= data_len) {
		fprintf(stderr, "No extensions present in TLS handshake header: %d\n", __LINE__);
		return NULL;
	}


	len = *p << 8; /* Extensions length high byte */
	p++;
	if (p - data >= data_len) {
		fprintf(stderr, "Did not receive complete TLS handshake header: %d\n", __LINE__);
		return NULL;
	}
	len += *p; /* Extensions length low byte */
	p++;

	while (1) {
		if (p - data + 4 >= data_len) { /* 4 bytes for the extension header */
			fprintf(stderr, "No more TLS handshake extensions: %d\n", __LINE__);
			return NULL;
		}

		/* Parse our extension header */
		len = (p[2] << 8) + p[3]; /* Extension length */
		if (p[0] == 0x00 && p[1] == 0x00) { /* Check if it's a server name extension */
			/* There can be only one extension of each type, so we break
			   our state and move p to beinging of the extension here */
			p += 4;
			if (p - data + len > data_len) {
				fprintf(stderr, "Did not receive complete TLS handshake header: %d\n", __LINE__);
				return NULL;
			}
			return parse_server_name_extension(p, len);
		}
		p += 4 + len; /* Advance to the next extension header */
	}
	return NULL;
}

const char *
parse_server_name_extension(const uint8_t* buf, int buf_len) {
	static char server_name[SERVER_NAME_LEN];
	const uint8_t* p = buf;
	int ext_len;
	uint8_t name_type;
	int name_len;

	if (p - buf + 1 > buf_len) {
		fprintf(stderr, "Incomplete server name extension: %d\n", __LINE__);
		return NULL;
	}

	ext_len = (p[0] << 8) + p[1];
	p += 2;

	while(1) {
		if (p - buf >= buf_len) {
			fprintf(stderr, "Incomplete server name extension: %d\n", __LINE__);
			return NULL;
		}
		name_type = *p;
		p ++;
		switch(name_type) {
			case(0x00):
				if (p - buf + 1 > buf_len) {
					fprintf(stderr, "Incomplete server name extension: %d\n", __LINE__);
					return NULL;
				}
				name_len = (p[0] << 8) + p[1];
				p += 2;
				if (p - buf + name_len > buf_len) {
					fprintf(stderr, "Incomplete server name extension: %d\n", __LINE__);
					return NULL;
				}
				if (name_len >= SERVER_NAME_LEN - 1) {
					fprintf(stderr, "Server name is too long\n");
					return NULL;
				}
				strncpy (server_name, (char *)p, name_len);
				server_name[name_len] = '\0';
				return server_name;
			default:
				fprintf(stderr, "Unknown name type in server name extension\n");
		}
	}
}

void hexdump(const void *ptr, int buflen) {
	const unsigned char *buf = (const unsigned char*)ptr;
	int i, j;
	for (i=0; i<buflen; i+=16) {
		printf("%06x: ", i);
		for (j=0; j<16; j++) 
			if (i+j < buflen)
				printf("%02x ", buf[i+j]);
			else
				printf("   ");
		printf(" ");
		for (j=0; j<16; j++) 
			if (i+j < buflen)
				printf("%c", isprint(buf[i+j]) ? buf[i+j] : '.');
		printf("\n");
	}
}

