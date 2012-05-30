export CC = gcc
export CFLAGS = -std=c99 -Wall -Wextra -pedantic -O3 -D_POSIX_C_SOURCE=200809L $(include_dirs) $(lib_dirs)

all: 
	${MAKE} -C src all

.PHONY: clean all test

clean:
	${MAKE} -C src clean
	${MAKE} -C tests clean

test: all
	${MAKE} -C tests test

