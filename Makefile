CC = gcc
CFLAGS = -ansi -Wall -Wextra -pedantic -O3

all: sni_proxy

%.o: %.c
	$(CC) $(CFLAGS) -c $<

sni_proxy: sni_proxy.o connection.o tls.o util.o backend.o
	$(CC) -o $@ $^

.PHONY: clean all

clean:
	rm -f *.o sni_proxy


