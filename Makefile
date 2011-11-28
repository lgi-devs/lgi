#
# LGI Dynamic GObject introspection binding.
#
# Author: Pavel Holejsovsky <pavel.holejsovsky@gmail.com>
# License: MIT
#

VERSION = 0.4

ROCK = lgi-$(VERSION)-1.rockspec

.PHONY : rock all clean install check

all : rock
	make -C lgi

rock : $(ROCK)
$(ROCK) : rockspec.in Makefile
	sed 's/%VERSION%/$(VERSION)/' $< >$@

clean :
	rm -f *.rockspec
	make -C lgi clean
	make -C tests clean

install :
	make -C lgi install

check :
	make -C tests check

export VERSION
