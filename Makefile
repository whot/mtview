SOURCES	= mtview
TARGETS	= mtview

TARG	= $(addprefix bin/,$(TARGETS))
TOBJ	= $(addprefix obj/,$(addsuffix .o,$(SOURCES)))

INCLUDE =
LIBS	= -lX11 -lutouch-frame -lutouch-evemu -lmtdev -lm

COMP	= gcc -O3 $(INCLUDE) -c $< -o $@
LINK	= gcc $< $(LIBS) -o $@

.PHONY: all man html clean mkobj
.PRECIOUS: obj/%.o

all:	$(TOBJ) $(TARG)

bin/%:	obj/%.o
	@mkdir -p $(@D)
	$(LINK)

obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(COMP)

man:
	@rm -rf man
	doxygen etc/doxman

html:
	@rm -rf html
	doxygen etc/doxhtml

clean:
	rm -rf man html bin obj auto
