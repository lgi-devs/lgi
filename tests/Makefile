#
# Makefile for compiling lgi testsuite support
#
# Author: Pavel Holejsovsky <pavel.holejsovsky@gmail.com>
# License: MIT
#

ifneq ($(filter CYGWIN%, $(shell uname -s)),)
EXT = .dll
PFX = cyg
LIBFLAG = -shared
else
EXT = .so
PFX = lib
LIBFLAG = -shared
CCSHARED = -fPIC
endif

PKGS = gio-2.0 cairo cairo-gobject gobject-introspection-1.0
LUA = lua

COPTFLAGS = -Wall -g
CFLAGS = $(CCSHARED) $(COPTFLAGS) $(LUA_CFLAGS) $(shell pkg-config --cflags $(PKGS))
LIBS += $(shell pkg-config --libs $(PKGS))
LDFLAGS = $(LIBFLAG)
DEPCHECK = .depcheck

# Precondition check
$(DEPCHECK) : Makefile
	pkg-config --exists '$(PKGS) >= 0.10.8' --print-errors
	touch $@

REGRESS = $(PFX)regress$(EXT)
REGRESS_OBJS = regress.o

.PHONY : all clean check

all : Regress-1.0.typelib

clean :
	rm -f $(REGRESS) $(REGRESS_OBJS) Regress-1.0.gir Regress-1.0.typelib

check : all
	cd .. && LD_LIBRARY_PATH=tests GI_TYPELIB_PATH=tests $(LUA) tests/test.lua

$(REGRESS) : regress.o
	$(CC) $(LDFLAGS) -o $@ regress.o $(LIBS)

GIDATADIR = $(shell pkg-config --variable=gidatadir gobject-introspection-1.0)/tests

regress.o : $(GIDATADIR)/regress.c $(GIDATADIR)/regress.h $(DEPCHECK)
	$(CC) $(CFLAGS) -c -o $@ $<

# Build .gir and .typelib
Regress-1.0.gir : $(REGRESS)
	g-ir-scanner --warn-all --no-libtool --quiet --output=$@	\
	  --namespace=Regress --nsversion=1.0				\
	  --include=cairo-1.0 --include=Gio-2.0				\
	  $(GIDATADIR)/regress.c $(GIDATADIR)/regress.h			\
	 -lregress

Regress-1.0.typelib : Regress-1.0.gir
	g-ir-compiler --output=$@ $<
