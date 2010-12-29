CC = gcc
CFLAGS = -ansi -Wall -Wextra -pedantic -O3 -g

all: sni_proxy

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

sni_proxy: sni_proxy.o connection.o tls.o util.o backend.o server.o
	$(CC) -o $@ $^

.PHONY: clean all

clean:
	rm -f *.o sni_proxy


