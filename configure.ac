#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT([sniproxy], [0.6.0])
AC_CONFIG_SRCDIR([src/sniproxy.c])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([subdir-objects])
AM_SILENT_RULES([yes])
AC_GNU_SOURCE

# Checks for programs.
AC_PROG_CC_C99
# Required by automake < 1.14
AM_PROG_CC_C_O


# Checks for libraries.
PKG_CHECK_MODULES([LIBEV], [libev], HAVE_LIBEV=yes; AC_DEFINE(HAVE_LIBEV, 1),
[AC_LIB_HAVE_LINKFLAGS(ev,, [#include <ev.h>], [ev_run(0,0);])
 if test x$ac_cv_libev = xyes; then
  AC_SUBST([LIBEV_LIBS], [$LIBEV])
 else
  AC_MSG_ERROR([[***
*** libev4 was not found.
***]])
 fi
])

PKG_CHECK_MODULES([LIBPCRE], [libpcre], HAVE_LIBPCRE=yes; AC_DEFINE(HAVE_LIBPCRE, 1),
[AC_LIB_HAVE_LINKFLAGS(pcre,, [#include <pcre.h>], [pcre_exec(0,0,0,0,0,0,0,0);])
 if test x$ac_cv_libpcre = xyes; then
  AC_SUBST([LIBPCRE_LIBS], [$LIBPCRE])
 else
  AC_MSG_ERROR([[***
*** libpcre was not found.
***]])
 fi
])

AC_ARG_ENABLE([dns],
  [AS_HELP_STRING([--disable-dns], [Disable DNS resolution])],
  [dns="$withval"], [dns=yes])

AM_CONDITIONAL([DNS_ENABLED], [test "x$dns" = "xyes"])

AS_IF([test "x$dns" = "xyes"],
 [PKG_CHECK_MODULES([LIBUDNS], [libudns], HAVE_LIBUDNS=yes; AC_DEFINE(HAVE_LIBUDNS, 1),
  [AC_LIB_HAVE_LINKFLAGS(udns,, [#include <udns.h>], [dns_init(0, 0);])
   AS_IF([test x$ac_cv_libudns = xyes], [AC_SUBST([LIBUDNS_LIBS], [$LIBUDNS])])
  ])
])

AC_ARG_ENABLE([rfc3339-timestamps],
  [AS_HELP_STRING([--enable-rfc3339-timestamps], [Enable RFC3339 timestamps])],
  [rfc3339_timestamps=${enableval}], [rfc3339_timestamps=no])

AS_IF([test "x$rfc3339_timestamps" = "xyes"],
    [AC_DEFINE([RFC3339_TIMESTAMP], 1, [RFC3339 timestamps enabled])])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h strings.h sys/socket.h sys/time.h syslog.h unistd.h],,
    AC_MSG_ERROR([required header(s) not found]))

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_UID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([atexit daemon memset socket strcasecmp strchr strdup strerror strncasecmp strrchr strspn strtoul],,
    AC_MSG_ERROR([required functions(s) not found]))

AC_CHECK_FUNCS([accept4])

# Enable large file support (so we can log more than 2GB)
AC_SYS_LARGEFILE

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 man/Makefile
                 tests/Makefile])

AC_OUTPUT
