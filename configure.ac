#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT([sniproxy], [0.1])
AC_CONFIG_SRCDIR([src/sniproxy.c])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([subdir-objects])

# Checks for programs.
AC_PROG_CC_C99
# Required by automake < 1.14
AM_PROG_CC_C_O

# Checks for libraries.
PKG_CHECK_MODULES([LIBEV], [libev],,
[AC_LIB_HAVE_LINKFLAGS(ev,, [#include <ev.h>], [ev_run(0,0);])
 if test x$ac_cv_libev = xyes; then
  AC_SUBST([LIBEV_LIBS], [$LIBEV])
 else
  AC_MSG_ERROR([[***
*** libev4 was not found.
***]])
 fi
])

PKG_CHECK_MODULES([LIBPCRE], [libpcre],,
[AC_LIB_HAVE_LINKFLAGS(pcre,, [#include <pcre.h>], [pcre_exec(0,0,0,0,0,0,0,0);])
 if test x$ac_cv_libpcre = xyes; then
  AC_SUBST([LIBPCRE_LIBS], [$LIBPCRE])
 else
  AC_MSG_ERROR([[***
*** libpcre was not found.
***]])
 fi
])

PKG_CHECK_MODULES([LIBUDNS], [libudns],,
[AC_LIB_HAVE_LINKFLAGS(udns,, [#include <udns.h>], [dns_init(0, 0);])
 if test x$ac_cv_libudns = xyes; then
  AC_SUBST([LIBUDNS_LIBS], [$LIBUDNS])
 fi
])

PKG_CHECK_MODULES([LIBRT], [librt],,
[AC_LIB_HAVE_LINKFLAGS(rt,, [#include <time.h>], [clock_gettime(CLOCK_MONOTONIC, NULL);])
 if test x$ac_cv_librt = xyes; then
  AC_SUBST([LIBRT_LIBS], [$LIBRT])
 else
  AC_MSG_ERROR([[***
*** librt was not found.
***]])
 fi
])

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
AC_CHECK_FUNCS([daemon memset socket strcasecmp strdup strerror strncasecmp],,
    AC_MSG_ERROR([required functions(s) not found]))

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 tests/Makefile])

AC_OUTPUT
