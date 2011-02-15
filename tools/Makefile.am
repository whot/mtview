bin_PROGRAMS = mtview

INCLUDES=-I$(top_srcdir)/include/

mtview_SOURCES = mtview.c
mtview_LDFLAGS = -lX11 -lutouch-frame -lutouch-evemu -lmtdev -lm

if HAVE_XI

AM_CFLAGS = $(XINPUT_CFLAGS)

mtview_LDFLAGS += $(XINPUT_LIBS)

endif
