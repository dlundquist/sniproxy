CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O3 -D_POSIX_SOURCE=1
LIBS = -lpcre

all: sni_proxy binder.o

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

sni_proxy: backend.o table.o connection.o tls.o http.o util.o listener.o server.o sni_proxy.o config.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean all

clean:
	rm -f *.o sni_proxy
