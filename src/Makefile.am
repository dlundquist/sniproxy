AM_CPPFLAGS = $(LIBEV_CFLAGS) $(LIBPCRE_CFLAGS)

sbin_PROGRAMS = sniproxy

sniproxy_SOURCES = address.c \
                   address.h \
                   backend.c \
                   backend.h \
                   buffer.c \
                   buffer.h \
                   cfg_parser.c \
                   cfg_parser.h \
                   cfg_tokenizer.c \
                   cfg_tokenizer.h \
                   config.c \
                   config.h \
                   connection.c \
                   connection.h \
                   http.c \
                   http.h \
                   listener.c \
                   listener.h \
                   logger.c \
                   logger.h \
				   protocol.h \
                   server.c \
                   server.h \
                   sniproxy.c \
                   sniproxy.h \
                   table.c \
                   table.h \
                   tls.c \
                   tls.h

sniproxy_LDADD = $(LIBEV_LIBS) $(LIBPCRE_LIBS)
