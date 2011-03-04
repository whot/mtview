SUBDIRS = tools

INCLUDES = $(top_srcdir)/include/

.PHONY: ChangeLog INSTALL
INSTALL:
	$(INSTALL_CMD)
ChangeLog:
	git log > ChangeLog

dist-hook: ChangeLog INSTALL
