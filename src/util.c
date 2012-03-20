#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h> /* memset */
#include "util.h"



void
hexdump(const void *ptr, int buflen) {
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

int isnumeric (const char * s)
{
    if (s == NULL || *s == '\0')
        return 0;
    char * p;
    strtod (s, &p);
    return *p == '\0';
}

