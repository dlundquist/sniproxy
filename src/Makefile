LIBS += -lpcre -lrt
MODULES =   sni_proxy.o \
            config.o \
            cfg_parser.o \
            cfg_tokenizer.o \
            util.o \
            server.o \
            listener.o \
            table.o \
            backend.o \
            connection.o \
            buffer.o \
            tls.o \
            http.o

all: sni_proxy

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

sni_proxy: $(MODULES)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean all

clean:
	rm -f *.o core sni_proxy
