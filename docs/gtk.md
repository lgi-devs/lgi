# Gtk support

LGI Gtk support is based on base gobject-introspection support.  Some
extensions are provided to support non-introspectable features and to
provide easier and more Lua-like access to some Gtk features.

## Basic Widget and Container support

### Style properties access

To read style property values of the widget, a `style` attribute is
implemented.  Following example reads `resize-grip-height` style
property from Gtk.Window instance:

    local window = Gtk.Window()
    print(window.style.resize_grip_height)
    
### Child properties

Child properties are properties of the relation between a container
and child.  A Lua-friendly access to these properties is implemented
by `children` attribute of `Gtk.Container`.  Following example
illustrates writing and reading of `x-padding` property of `Gtk.Table`
and child `Gtk.Button`:

    local table, button = Gtk.Table(), Gtk.Button()
    table:add(button)
    table.children[button].x_padding = 10
    print(table.children[button].x_padding)   -- prints 10

`Gtk.Container:add()` is overloaded so that it accepts optional second
argument containing table with child property/value pairs.  Previous
example can be rewritten like this:

    local table, button = Gtk.Table(), Gtk.Button()
    table:add(button, { x_padding = 10 })
    print(table.children[button].x_padding)   -- prints 10
