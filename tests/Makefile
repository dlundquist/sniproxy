CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O3 -g -I ../src -L ../src
TESTS = http.test tls.test

all: ${TESTS}

%.test: %.o ../src/%.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

.PHONY: clean all

clean:
	rm -f *.o *.test

test: all
	./run_tests.sh
