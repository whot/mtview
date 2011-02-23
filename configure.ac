# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([Multitouch Viewer],
        [1.1.3],
        [],
        [mtview])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([config-aux])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_MAINTAINER_MODE

LIB_VERSION=1:0:0
AC_SUBST([LIB_VERSION])

# Initialize libtool
AC_PROG_LIBTOOL

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

PKG_CHECK_MODULES([MTDEV], [mtdev >= 1.1])
PKG_CHECK_MODULES([EVEMU], [utouch-evemu >= 1.0])
PKG_CHECK_MODULES([FRAME], [utouch-frame >= 1.1])
PKG_CHECK_MODULES([X11], [x11])

AC_ARG_WITH([xi], AS_HELP_STRING([--with-xi], [Build with XI2.1 support]))
AM_CONDITIONAL([HAVE_XI], [test "x$with_xi" != "x"])

AS_IF([test "x$with_xi" = "xyes"], [
   PKG_CHECK_MODULES(XINPUT, x11 xext [xi >= 1.4.1.99.1] [inputproto >= 2.0.99.1])
   AC_DEFINE([HAVE_XI], [1], [XI2.1 support])
])

AC_CONFIG_FILES([Makefile
                 tools/Makefile
                 mtview.pc])
AC_OUTPUT
