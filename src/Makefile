CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O3 -D_POSIX_SOURCE=1

all: sni_proxy

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

sni_proxy: connection.o tls.o util.o backend.o server.o sni_proxy.o config.o http.o
	$(CC) $(CFLAGS) -lpcre -o $@ $^

.PHONY: clean all

clean:
	rm -f *.o sni_proxy
