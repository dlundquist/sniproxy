all: 
	make -C src all

.PHONY: clean all test

clean:
	make -C src clean
	make -C tests clean

test: all
	make -C tests test

