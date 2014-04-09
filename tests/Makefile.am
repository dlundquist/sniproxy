AM_CPPFLAGS = -I$(top_srcdir)/src -g $(LIBEV_CFLAGS) $(LIBPCRE_CFLAGS) $(LIBRT_CFLAGS) $(LIBUDNS_LIBS)

TESTS = http_test \
        tls_test \
        buffer_test \
        address_test \
        cfg_tokenizer_test \
        config_test \
        resolv_test \
        functional_test \
        bad_request_test \
        slow_client_test \
        connection_reset_test

check_PROGRAMS = http_test \
                 tls_test \
                 buffer_test \
                 cfg_tokenizer_test \
                 address_test \
                 resolv_test \
                 config_test

http_test_SOURCES = http_test.c \
                    ../src/http.c

tls_test_SOURCES = tls_test.c \
                   ../src/tls.c \
                   ../src/logger.c

buffer_test_SOURCES = buffer_test.c \
                      ../src/buffer.c

buffer_test_LDADD = -lrt

address_test_SOURCES = address_test.c \
                      ../src/address.c

cfg_tokenizer_test_SOURCES = cfg_tokenizer_test.c \
                             ../src/cfg_tokenizer.c

config_test_SOURCES = config_test.c \
                      ../src/config.c \
                      ../src/cfg_parser.c \
                      ../src/cfg_tokenizer.c \
                      ../src/address.c \
                      ../src/backend.c \
                      ../src/table.c \
                      ../src/listener.c \
                      ../src/connection.c \
                      ../src/buffer.c \
                      ../src/logger.c \
                      ../src/resolv.c \
                      ../src/resolv.h \
                      ../src/tls.c \
                      ../src/http.c

config_test_LDADD = $(LIBEV_LIBS) $(LIBPCRE_LIBS) $(LIBRT_LIBS) $(LIBUDNS_LIBS)

resolv_test_SOURCES = resolv_test.c \
                      ../src/resolv.c \
                      ../src/address.c \
                      ../src/logger.c

resolv_test_LDADD = $(LIBEV_LIBS) $(LIBUDNS_LIBS)

