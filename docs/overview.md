# LGI Overview

LGI is Lua binding to Gnome platform.  It is implemented as dynamic
binding using gobject-introspection.  This means that all libraries
with support for gobject-introspection can be used by LGI without any
need to compile/install anything, assuming that proper .typelib file
is installed and available.

## Installation

### Dependencies

LGI depends on `gobject-introspection >= 1.30` package.  To build,
gobject-introspection development package must also be installed.
Note that required gobject-introspection version is unfortunately
rather new, currently mostly available only in unreleased-yet versions
of major distributions (part of GNOME-3.2, e.g. Fedora 16).  There is
planned work to make LGI mostly work also with older
gobject-introspection versions, which are part of GNOME-3.0.  Pre-3.0
versions are not planned to be supported at all.

In order to be able to use assorted gobject-based libraries through
LGI, these libraries must have properly installed `.typelib` files.
Most, if not all distributions already do this properly.

### Supported platforms

LGI is currently tested on Linux (all sane Linux distributions should work
fine) and Cygwin.  There is no principal obstacle for supporting other
platforms, as long as gobject-introspection library (and of course Lua) is
ported and working there.

### Installing via LuaRocks

The preferred way to install LGI is using luarocks.  As of writing
this document, LGI is not yet available on public luarocks server, so
plain `luarocks install lgi` does not work yet, although it will be
preferred way to install LGI in the future.  Currently, LGI source
must be downloaded, unpacked and installed using `luarocks make`.

### Installing using Makefile

Another way to install LGI is using makefiles:

    make
    sudo make install [PREFIX=prefix-path] [DESTDIR=destir-path]

Default `PREFIX` is `/usr/local` and default `DESTDIR` is empty.

## Quick overview

All LGI functionality is available in Lua module lgi, which is loaded
by using Lua `require` construct:

    local lgi = require 'lgi'

All gobject-introspection accessible modules are now accessible in lgi table:

    local Gtk = lgi.Gtk
    local Gio = lgi.Gio
    local GLib = lgi.GLib

To create instance of the class, simply 'call' the class in the namespace:

    local window = Gtk.Window()

To access object properties and call methods on the object instances,
use normal Lua object access notation:

    window.title = 'I am a window'
    window:show_all()
    window.title = window.title .. ' made by Lgi'

Note that properties can have `-` (dash) character in them.  It is
illegal in Lua, so it is translated to `_` (underscore).

    window.has_resize_grip = true

It is also possible to assign properties during object construction:

    local window = Gtk.Window {
       title = 'I am a window made by Lgi',
       has_resize_grip = true
    }

Note that structures and unions are handled similarly to classes, but
structure fields are accessed instead of properties.

To connect signal to object instance, assign function to be run to
`on_signalname` object slot:

    window.on_destroy = function(object)
                           print('destroying', object)
                        end

Note that Lua has nice syntactic sugar for objects, so previous
construction can also be written like this:

    function window:on_destroy()
       print('destroying', self)
    end

Note that potential dashes in signal names are also translated to
underscores to cope well with Lua identifier rules.

Enumerations and bitflags are grouped in the enumeration name table,
and real names are enumeration nicks uppercased.  For example,
`GTK_WINDOW_TOPLEVEL` identifier is accessible as
`Gtk.WindowType.TOPLEVEL`.

There is no need to handle any kind of memory management; LGI handles
all reference counting internally in cooperation with Lua's garbage
collector.

For APIs which use callbacks, provide Lua function which will be
called when the callback is invoked.  It is also possible to pass
coroutine instance as callback argument, in this case, coroutine is
resumed and returning `coroutine.yield()` returns all arguments passed
to the callback.  The callback returns when coroutine yields again or
finishes.  Arguments passed to `coroutine.yield()` call or exit status
of the coroutine are then used as return value from the callback.

See examples in `samples` source directory to dive deeper into the LGI
features.
