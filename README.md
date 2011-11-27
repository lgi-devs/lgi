# LGI

GObject-Introspection-based Lua binding to GObject based libraries.

Licensed under MIT-style license
http://www.opensource.org/licenses/mit-license.php, see LICENSE file
for full text.

## Installation:

In order to be able to compile native part of lgi,
gobject-introspection >= 0.10.8 development package must be installed,
although preferred version is >= 1.30.  The development package is
called `libgirepository1.0-dev` on debian-based systems (like Ubuntu)
and `gobject-introspection-devel` on RedHat-based systems (like Fedora).

Using LuaRocks:

    luarocks install lgi

Alternatively, use make-based installation

    make
    [sudo] make install [PREFIX=<prefix>] [DESTDIR=<destdir>]

## Usage:
See examples in samples/ directory.  Documentation is available in
doc/ directory in markdown format.  Process it with your favorite
markdown processor if you want to read it in HTML.
