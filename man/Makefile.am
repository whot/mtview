man_pages_src = mtview.txt

if HAVE_DOCTOOLS
man_pages = $(man_pages_src:.txt=.1)

SUFFIXES = .1 .txt .xml

.xml.1:
	@$(XMLTO) man $<

.txt.xml:
	@$(ASCIIDOC) -b docbook -d manpage -o $@ $<

dist_man_MANS = $(man_pages)

EXTRA_DIST = $(man_pages_src) $(man_pages)
CLEANFILES = $(dist_man_MANS)
MAINTAINERCLEANFILES = $(man_pages) *.xml
endif
