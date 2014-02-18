AM_CPPFLAGS = -I$(top_srcdir)/src -g $(LIBEV_CFLAGS) $(LIBPCRE_CFLAGS)

TESTS = http_test \
                tls_test \
                buffer_test \
                address_test \
                cfg_tokenizer_test \
                config_test \
                functional_test \
                bad_request_test \
                connection_reset_test

check_PROGRAMS = http_test \
                 tls_test \
                 buffer_test \
                 cfg_tokenizer_test \
                 address_test \
                 config_test

http_test_SOURCES = http_test.c \
                    ../src/http.c

tls_test_SOURCES = tls_test.c \
                   ../src/tls.c \
                   ../src/logger.c

buffer_test_SOURCES = buffer_test.c \
                      ../src/buffer.c

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
                      ../src/tls.c \
                      ../src/http.c

config_test_LDADD = $(LIBEV_LIBS) $(LIBPCRE_LIBS)
