CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O3 -D_POSIX_C_SOURCE=200809L -g -I ../src -L ../src
TESTS = http.test tls.test config.test binder.test

all: ${TESTS}

%.test: %.o ../src/%.o ../src/cfg_parser.o ../src/cfg_tokenizer.o ../src/util.o ../src/backend.o
	$(CC) $(CFLAGS) -o $@ $^ -l pcre

%.o: %.c %.h
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean all

clean:
	rm -f *.o ${TESTS}

test: all
	./run_tests.sh
