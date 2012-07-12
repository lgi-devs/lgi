#
# Makefile for compiling lgi core module in standard-Lua variant
#
# Copyright (C) 2010-2012 Pavel Holejsovsky
# License: MIT
#

ifndef LUA_VERSION
# A bit wacky attempt at automatic detection whetehr we are compiling
# against Lua 5.1 or Lua 5.2 headers.
LUA_VERSION_DOTTED := $(shell				\
	echo '\#include "lua.h"' |			\
	$(CC) $(CFLAGS) -dM -E - |			\
	sed '/LUA_VERSION_NUM/!d;s/^[^0-9]*//;s/0/\./')
LUA_VERSION := $(shell echo $(LUA_VERSION_DOTTED) | sed 's/\.//')
endif

PREFIX = /usr/local
LUA_LIBDIR = $(PREFIX)/lib/lua/$(LUA_VERSION_DOTTED)
LUA_SHAREDIR = $(PREFIX)/share/lua/$(LUA_VERSION_DOTTED)

GINAME = gobject-introspection-1.0
PKGS = $(GINAME) gmodule-2.0 libffi

ifneq ($(filter CYGWIN%, $(shell uname -s)),)
CORE = lgilua$(LUA_VERSION).dll
LIBFLAG = -shared
LIBS += -llua
else
CORE = lgilua$(LUA_VERSION).so
LIBFLAG = -shared
CCSHARED = -fPIC
endif

OBJS = $(addprefix $(LUA_VERSION),		\
	 lgi.o					\
	 buffer.o				\
	 aggr.o					\
	 compound.o				\
	 ctype.o				\
	 call.o)

CFLAGS = -Wall -Wextra -O2 -g
ALLCFLAGS = $(CCSHARED) $(CFLAGS) $(LUA_CFLAGS) \
$(shell pkg-config --cflags $(PKGS))
LIBS += $(shell pkg-config --libs $(PKGS))
LDFLAGS = $(LIBFLAG)
DEPCHECK = .depcheck-$(LUA_VERSION)

all : $(CORE)

# Precondition check
$(DEPCHECK) : Makefile
	pkg-config --exists '$(GINAME) >= 0.10.8' --print-errors
	touch $@

.PHONY : all clean install

clean :
	rm -f $(CORE) $(OBJS)

$(CORE) : $(OBJS) $(DEPCHECK)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(LUA_VERSION)%.o : %.c $(DEPCHECK)
	$(CC) $(ALLCFLAGS) -c $< -o $@

$(LUA_VERSION)lgi.o : lgi.c lgi.h $(DEPCHECK)
$(LUA_VERSION)buffer.o : buffer.c lgi.h $(DEPCHECK)
$(LUA_VERSION)aggr.o : aggr.c lgi.h $(DEPCHECK)
$(LUA_VERSION)compound.o : compound.c lgi.h $(DEPCHECK)
$(LUA_VERSION)ctype.o : ctype.c lgi.h $(DEPCHECK)
$(LUA_VERSION)call.o : call.c lgi.h $(DEPCHECK)

install : $(CORE) $(VERSION_FILE)
	mkdir -p $(DESTDIR)$(LUA_LIBDIR)/lgi/core/lua5
	cp $(CORE) $(DESTDIR)$(LUA_LIBDIR)/lgi/core/lua5
	mkdir -p $(DESTDIR)$(LUA_SHAREDIR)/lgi/core/lua5
	cp $(wildcard *.lua) $(DESTDIR)$(LUA_SHAREDIR)/lgi/core/lua5
