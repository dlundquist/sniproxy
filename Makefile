export CC = gcc
export CFLAGS = -std=c99 -Wall -Wextra -Wfatal-errors -pedantic-errors -O3 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=500 $(include_dirs) $(lib_dirs)

all:
	${MAKE} -C src all

.PHONY: clean all test

clean:
	${MAKE} -C src clean
	${MAKE} -C tests clean

test: all
	${MAKE} -C tests test

