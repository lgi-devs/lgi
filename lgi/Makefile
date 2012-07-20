#
# Makefile for compiling lgi core module in standard-Lua variant
#
# Copyright (C) 2010-2012 Pavel Holejsovsky
# License: MIT
#

PREFIX = /usr/local
LUA_LIBDIR = $(PREFIX)/lib/lua/5.1
LUA_SHAREDIR = $(PREFIX)/share/lua/5.1

VERSION_FILE = version.lua

all : $(VERSION_FILE)

clean :
	rm -f $(VERSION_FILE)

$(VERSION_FILE) : Makefile ../Makefile
	echo "return '$(VERSION)'" > $@

OVERRIDES = $(wildcard override/*.lua)
CORESOURCES = $(wildcard *.lua)

install : $(VERSION_FILE)
	mkdir -p $(DESTDIR)$(LUA_SHAREDIR)
	cp ../lgi.lua $(DESTDIR)$(LUA_SHAREDIR)
	mkdir -p $(DESTDIR)$(LUA_SHAREDIR)/lgi
	cp $(CORESOURCES) $(VERSION_FILE) $(DESTDIR)$(LUA_SHAREDIR)/lgi
	mkdir -p $(DESTDIR)$(LUA_SHAREDIR)/lgi/override
	cp $(OVERRIDES) $(DESTDIR)$(LUA_SHAREDIR)/lgi/override
