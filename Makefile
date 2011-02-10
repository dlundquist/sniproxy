CC = gcc
CFLAGS = -ansi -Wall -Wextra -pedantic -O3

all: sni_proxy

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

sni_proxy: connection.o tls.o util.o backend.o server.o sni_proxy.o config.o
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean all

clean:
	rm -f *.o sni_proxy
