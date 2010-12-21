CC = gcc
CFLAGS = -ansi -Wall -Wextra -pedantic -O3

all: sni_proxy

%.o: %.c
	$(CC) $(CFLAGS) -c $<

sni_proxy: sni_proxy.o connection.o
	$(CC) -o $@ $^

.PHONY: clean all

clean:
	rm -f *.o


