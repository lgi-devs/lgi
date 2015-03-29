#
# Makefile for compiling lgi testsuite support
#
# Author: Pavel Holejsovsky <pavel.holejsovsky@gmail.com>
# License: MIT
#

HOST_OS = $(shell uname -s | tr A-Z a-z)

ifneq ($(filter cygwin% msys% mingw%, $(HOST_OS)),)
EXT = .dll
PFX = cyg
LIBFLAG = -shared
else
ifeq ($(HOST_OS),darwin)
EXT = .so
PFX = lib
LIBFLAG = -bundle -undefined dynamic_lookup
CCSHARED = -fno-common
else
EXT = .so
PFX = lib
LIBFLAG = -shared
CCSHARED = -fPIC
endif
endif

PKGS = gio-2.0 cairo cairo-gobject gobject-introspection-1.0 gmodule-2.0 libffi
LUA = lua
PKG_CONFIG = pkg-config

ifndef CFLAGS
ifndef COPTFLAGS
CFLAGS = -Wall -g
endif
endif
ALL_CFLAGS = $(CCSHARED) $(COPTFLAGS) $(LUA_CFLAGS) $(shell $(PKG_CONFIG) --cflags $(PKGS)) $(CFLAGS) -I .
LIBS += $(shell $(PKG_CONFIG) --libs $(PKGS))
ALL_LDFLAGS = $(LIBFLAG) $(LDFLAGS)
DEPCHECK = .depcheck

# Precondition check
$(DEPCHECK) : Makefile
	$(PKG_CONFIG) --exists '$(PKGS) >= 0.10.8' --print-errors
	touch $@

REGRESS = $(PFX)regress$(EXT)
REGRESS_OBJS = regress.o

.PHONY : all clean check

all : Regress-1.0.typelib

clean :
	rm -f $(REGRESS) $(REGRESS_OBJS) Regress-1.0.gir Regress-1.0.typelib

check : all
	cd .. && LD_LIBRARY_PATH=tests:$$LD_LIBRARY_PATH \
	    GI_TYPELIB_PATH=tests:$$GI_TYPELIB_PATH \
	    LUA_PATH=./?.lua\;`$(LUA) -e "print(package.path)"` \
	    LUA_CPATH=./?.so\;`$(LUA) -e "print(package.cpath)"` \
	    $(LUA) tests/test.lua

$(REGRESS) : regress.o
	$(CC) $(ALL_LDFLAGS) -o $@ regress.o $(LIBS)

GIDATADIR = $(shell $(PKG_CONFIG) --variable=gidatadir gobject-introspection-1.0)/tests

regress.o : $(GIDATADIR)/regress.c $(GIDATADIR)/regress.h $(DEPCHECK)
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

# Build .gir and .typelib
Regress-1.0.gir : $(REGRESS)
	LDFLAGS="" CFLAGS=""						\
	g-ir-scanner --warn-all --no-libtool --quiet --output=$@	\
	  --namespace=Regress --nsversion=1.0				\
	  --include=cairo-1.0 --include=Gio-2.0				\
	  --library-path=/usr/lib --library-path=/usr/X11R6/lib		\
	  --library-path=/usr/local/lib					\
	  $(GIDATADIR)/regress.c $(GIDATADIR)/regress.h			\
	 -lregress

Regress-1.0.typelib : Regress-1.0.gir
	g-ir-compiler --output=$@ $<
