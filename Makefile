#
# LGI Dynamic GObject introspection binding.
#
# Author: Pavel Holejsovsky <pavel.holejsovsky@gmail.com>
# License: MIT
#

VERSION = 0.9.0
MAKE ?= make

ROCK = lgi-$(VERSION)-1.rockspec

.PHONY : rock all clean install check

all :
	$(MAKE) -C lgi

rock : $(ROCK)
$(ROCK) : rockspec.in Makefile
	sed 's/%VERSION%/$(VERSION)/' $< >$@

clean :
	rm -f *.rockspec
	$(MAKE) -C lgi clean
	$(MAKE) -C tests clean

install :
	$(MAKE) -C lgi install

check : all
	$(MAKE) -C tests check

export VERSION
