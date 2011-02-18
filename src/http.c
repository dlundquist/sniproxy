#include <stdio.h>
#include <string.h> /* strncpy() */
#include <strings.h> /* strncasecmp() */
#include <ctype.h> /* isblank() */
#include "http.h"
#include "util.h"

#define SERVER_NAME_LEN 256


static int next_header(const char **, int *);
static char *get_header(const char *, const char *, int);


const char *
parse_http_header(const char* data, int len) {
    char *hostname;
    int i;

    hostname = get_header("Host:", data, len);
    if (hostname == NULL)
        return hostname;

    /* 
     *  if the user specifies the port in the request, it is included here.
     *  Host: example.com:80
     *  so we trim off port portion
     */
    for (i = strlen(hostname); i > 0; i--)
        if (hostname[i] == ':') {
            hostname[i] = '\0';
            break;
        }

    return hostname;
}

static char *
get_header(const char *header, const char *data, int data_len) {
    static char header_data[SERVER_NAME_LEN];
    int len, header_len;

    header_len = strlen(header);

    /* loop through headers stopping at first blank line */
    while ((len = next_header(&data, &data_len)) != 0)
        if (len > header_len && strncasecmp(header, data, header_len) == 0) {
            /* Eat leading whitespace */
            while (header_len < len && isblank(data[header_len]))
                header_len++;

            /* Check if we have enought room before copying */
            if (len - header_len >= SERVER_NAME_LEN) {
                /* too big */
                return NULL;
            }
            strncpy (header_data, data + header_len, len - header_len);
            
            /* null terminate the header data */
            header_data[len - header_len] = '\0';

            return header_data;
        }

    return NULL;
}

static int
next_header(const char **data, int *len) {
    int header_len;

    /* perhaps we can optimize this to reuse the value of header_len, rather than scanning twice */
    /* walk our data stream until the end of the header */
    while (*len > 2 && (*data)[0] != '\r' && (*data)[1] != '\n') {
        (*len) --;
        (*data) ++;
    }

    /* advanced past the <CR><LF> pair */
    *data += 2;
    *len -= 2;

    /* Find the length of the next header */
    header_len = 0;
    while (*len > header_len + 1
            && (*data)[header_len] != '\r'
            && (*data)[header_len + 1] != '\n')
        header_len++;

    return header_len;
}
