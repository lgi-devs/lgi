# LGI Overview

LGI is Lua binding to Gnome platform.  It is implemented as dynamic
binding using gobject-introspection.

## Installation

Installs either using luarocks (TODO) or included waf installer.
Depends on Gnome-3.2 modules (specifically, glib>=2.30,
gobject-introspection>=1.30, gtk3 etc).

## Usage

All LGI functionality is available in Lua module lgi, which is loaded
by using Lua `require` construct:

    local lgi = require 'lgi'

All gobject-introspection accessible modules are now accessible in lgi table:

    local Gtk = lgi.Gtk
    local Gio = lgi.Gio
    local GLib = lgi.GLib

It is also possible to ensure that exact version of the package is
used by custom `lgi.require` function:

    local Gtk = lgi.require('Gtk', '3.0')
    local Vte = lgi.require('Vte', '2.90')

Note that version is taken from appropriate gobject-introspection
`.typelib` file, so consult your `/usr/lib[64]/girepository-1.0` folder to
see which versions of which packages are available.

The loaded package table then contains all toplevel elements from the
appropriate typelib file, i.e. all classes, structs, unions,
constants, and enums exported by the package.  For example,
declarations above make following elements are available:
`Gtk.Window`, `Gio.File`, `Gio.ApplicationFlags`,
`GLib.PRIORITY_DEFAULT`


### Creating class and structure instances

Once the package is loaded, it is possible to create instances of the
classes by 'calling' them, e.g.

    local Gtk, Gdk = lgi.Gtk, lgi.Gdk
    local window = Gtk.Window()
    local rect = Gdk.Rectangle()

creates new Gtk.Window class and Gdk.Rectangle structure instances.

When creating classes, it is possible to specify table of properties
as argument, like in following example:

    local window = Gtk.Window { title = 'LGI Window',
                                type = Gtk.WindowType.TOPLEVEL }

Structures allow similar construct to initialize its fields:

    local rect = Gdk.Rectangle { x = 1, y = 2, width = 10, height = 20 }

Note that if the structure provides constructor called `new`,
structure constructor is mapped to it directly, e.g.

    local
