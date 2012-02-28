# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([Multitouch Viewer],
        [1.1.7],
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

LT_LIB_M

PKG_CHECK_MODULES([MTDEV], [mtdev >= 1.1])
PKG_CHECK_MODULES([EVEMU], [utouch-evemu >= 1.0])
PKG_CHECK_MODULES([FRAME], [utouch-frame >= 1.1])
PKG_CHECK_MODULES([X11], [x11])


PKG_CHECK_MODULES(XINPUT, x11 xext [xi >= 1.5.99.1] [inputproto >= 2.1.99.6],
                  HAVE_XI22="yes"; AC_DEFINE(HAVE_XI22, 1, [XI22 available]),
                  HAVE_XI22="no");
AM_CONDITIONAL([HAVE_XI22], [test "HAVE_XI22" = "yes"])

AC_CONFIG_FILES([Makefile
                 tools/Makefile])
AC_OUTPUT
