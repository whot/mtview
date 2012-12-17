# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([Multitouch Viewer],
        [1.9.99.1],
        [],
        [mtview])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([config-aux])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign no-dist-gzip dist-bzip2])
AM_MAINTAINER_MODE([enable])

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

OLD_CFLAGS="$CFLAGS"
OLD_LDFLAGS="$LDFLAGS"
CFLAGS="$CFLAGS $EVEMU_CFLAGS"
LDFLAGS="$LDFLAGS $EVEMU_LIBS"
AC_CHECK_LIB(utouch-evemu, evemu_get_abs_current_value,
	     AC_DEFINE(EVEMU_HAVE_GET_ABS_CURRENT_VALUE, 1, [evemu provides evemu_get_abs_current_value]))
CFLAGS="$OLD_CFLAGS"
LDFLAGS="$OLD_LDFLAGS"


PKG_CHECK_MODULES([X11], [x11])
PKG_CHECK_MODULES([CAIRO], [cairo])

AC_ARG_VAR([XMLTO], [Path to xmlto command])
AC_PATH_PROG([XMLTO], [xmlto])
AC_ARG_VAR([ASCIIDOC], [Path to asciidoc command])
AC_PATH_PROG([ASCIIDOC], [asciidoc])
AM_CONDITIONAL([HAVE_DOCTOOLS], [test "x$XMLTO" != "x" && test "x$ASCIIDOC" != "x"])
if test "x$XMLTO" = "x" || test "x$ASCIIDOC" = "x"; then
	AC_MSG_WARN([xmlto or asciidoc not found - cannot create man pages without it])
fi

AC_CONFIG_FILES([Makefile
                 tools/Makefile
                 man/Makefile])
AC_OUTPUT
