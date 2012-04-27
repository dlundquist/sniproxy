CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O3 -D_POSIX_C_SOURCE=200809L -g -I ../src -L ../src
TESTS = http.test \
        tls.test \
        config.test \
        buffer.test \
        binder.test \
        cfg_tokenizer.test

all: ${TESTS}

config.test: config.o ../src/config.o ../src/cfg_parser.o ../src/cfg_tokenizer.o ../src/util.o ../src/backend.o ../src/table.o ../src/listener.o ../src/connection.o ../src/buffer.o ../src/tls.o ../src/http.o
	$(CC) $(CFLAGS) -o $@ $^ -l pcre

%.test: %.o ../src/%.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c %.h
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean all

clean:
	rm -f *.o core ${TESTS}

test: all
	./run_tests.sh
