#
# Makefile for compiling lgi core module in standard-Lua variant
#
# Copyright (C) 2010-2012 Pavel Holejsovsky
# License: MIT
#

PREFIX = /usr/local
LUA_LIBDIR = $(PREFIX)/lib/lua/5.1
LUA_SHAREDIR = $(PREFIX)/share/lua/5.1

GINAME = gobject-introspection-1.0
PKGS = $(GINAME) gmodule-2.0 libffi

ifneq ($(filter CYGWIN%, $(shell uname -s)),)
CORE = lgilua51.dll
LIBFLAG = -shared
LIBS += -llua
else
CORE = lgilua51.so
LIBFLAG = -shared
CCSHARED = -fPIC
endif

OBJS = lgi.o buffer.o aggr.o compound.o ctype.o call.o

COPTFLAGS = -Wall -Wextra -O2 -g
CFLAGS = $(CCSHARED) $(COPTFLAGS) $(LUA_CFLAGS) $(shell pkg-config --cflags $(PKGS))
LIBS += $(shell pkg-config --libs $(PKGS))
LDFLAGS = $(LIBFLAG)
DEPCHECK = .depcheck

all : $(CORE)

# Precondition check
$(DEPCHECK) : Makefile
	pkg-config --exists '$(GINAME) >= 0.10.8' --print-errors
	touch $@

.PHONY : all clean install

clean :
	rm -f $(CORE) $(OBJS)

$(CORE) : $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

lgi.o : lgi.c lgi.h $(DEPCHECK)
buffer.o : buffer.c lgi.h $(DEPCHECK)
aggr.o : aggr.c lgi.h $(DEPCHECK)
compound.o : compound.c lgi.h $(DEPCHECK)
ctype.o : ctype.c lgi.h $(DEPCHECK)
call.o : call.c lgi.h $(DEPCHECK)

install : $(CORE) $(VERSION_FILE)
	mkdir -p $(DESTDIR)$(LUA_LIBDIR)/lgi/core/lua5
	cp $(CORE) $(DESTDIR)$(LUA_LIBDIR)/lgi/core/lua5
