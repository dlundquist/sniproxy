#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT([sniproxy], [0.1])
AC_CONFIG_SRCDIR([src/sniproxy.c])
AM_INIT_AUTOMAKE([subdir-objects])

# Checks for programs.
AC_PROG_CC_C99

# Checks for libraries.
AC_SEARCH_LIBS([ev_run], [ev],,AC_MSG_ERROR([libev4 not found]))
AC_SEARCH_LIBS([pcre_exec], [pcre],,AC_MSG_ERROR([libpcre not found]))

# Checks for header files.
AC_FUNC_ALLOCA
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h stddef.h stdlib.h string.h strings.h sys/socket.h sys/time.h syslog.h unistd.h],,
    AC_MSG_ERROR([required header(s) not found]))

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([daemon memset socket strcasecmp strdup strerror strncasecmp clock_gettime],,
    AC_MSG_ERROR([required functions(s) not found]))

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 tests/Makefile])

AC_OUTPUT
