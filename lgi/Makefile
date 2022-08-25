#
# Makefile for compiling lgi core module in standard-Lua variant
#
# Author: Pavel Holejsovsky <pavel.holejsovsky@gmail.com>
# License: MIT
#

PREFIX = /usr/local
HOST_OS = $(shell uname -s | tr A-Z a-z)
LUA_VERSION=5.1
LUA_LIBDIR = $(PREFIX)/lib/lua/$(LUA_VERSION)
LUA_SHAREDIR = $(PREFIX)/share/lua/$(LUA_VERSION)

PKG_CONFIG ?= pkg-config
GINAME = gobject-introspection-1.0
PKGS = $(GINAME) gmodule-2.0 libffi
VERSION_FILE = version.lua

LUA_LIB = -llua

ifneq ($(filter cygwin% msys% mingw%, $(HOST_OS)),)
CORE = corelgilua51.dll
LIBFLAG = -shared
LIBS += $(LUA_LIB)
else
ifeq ($(HOST_OS),darwin)
CORE = corelgilua51.so
LIBFLAG = -bundle -undefined dynamic_lookup
CCSHARED = -fno-common
GOBJECT_INTROSPECTION_LIBDIR = $(shell $(PKG_CONFIG) --variable=libdir $(GINAME))
else
CORE = corelgilua51.so
LIBFLAG = -shared
CCSHARED = -fPIC
endif
endif

OBJS = buffer.o callable.o core.o gi.o marshal.o object.o record.o

ifndef CFLAGS
ifndef COPTFLAGS
CFLAGS = -Wall -Wextra -O2 -g
endif
endif
ifeq ($(HOST_OS),darwin)
CFLAGS += -DGOBJECT_INTROSPECTION_LIBDIR=\"$(GOBJECT_INTROSPECTION_LIBDIR)\"
endif
ALL_CFLAGS = $(CCSHARED) $(COPTFLAGS) $(LUA_CFLAGS) $(shell $(PKG_CONFIG) --cflags $(PKGS)) $(CFLAGS)
LIBS += $(shell $(PKG_CONFIG) --libs $(PKGS))
ALL_LDFLAGS = $(LIBFLAG) $(LDFLAGS)
DEPCHECK = .depcheck

# Precondition check
$(DEPCHECK) : Makefile
	$(PKG_CONFIG) --exists '$(GINAME) >= 0.10.8' --print-errors
	touch $@

.PHONY : all clean install

all : $(CORE) $(VERSION_FILE)
clean :
	rm -f $(CORE) $(OBJS)

%.o : %.c
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) -c -o $@ $<

$(CORE) : $(OBJS)
	$(CC) $(ALL_LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(VERSION_FILE) : Makefile ../Makefile
	echo "return '$(VERSION)'" > $@

buffer.o : buffer.c lgi.h $(DEPCHECK)
callable.o : callable.c lgi.h $(DEPCHECK)
core.o : core.c lgi.h $(DEPCHECK)
gi.o : gi.c lgi.h $(DEPCHECK)
marshal.o : marshal.c lgi.h $(DEPCHECK)
object.o : object.c lgi.h $(DEPCHECK)
record.o : record.c lgi.h $(DEPCHECK)

OVERRIDES = $(wildcard override/*.lua)
CORESOURCES = $(wildcard *.lua)

install : $(CORE) $(VERSION_FILE)
	mkdir -p $(DESTDIR)$(LUA_LIBDIR)/lgi
	cp $(CORE) $(DESTDIR)$(LUA_LIBDIR)/lgi
	mkdir -p $(DESTDIR)$(LUA_SHAREDIR)
	cp ../lgi.lua $(DESTDIR)$(LUA_SHAREDIR)
	mkdir -p $(DESTDIR)$(LUA_SHAREDIR)/lgi
	cp $(CORESOURCES) $(DESTDIR)$(LUA_SHAREDIR)/lgi
	mkdir -p $(DESTDIR)$(LUA_SHAREDIR)/lgi/override
	cp $(OVERRIDES) $(DESTDIR)$(LUA_SHAREDIR)/lgi/override
