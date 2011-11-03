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

## Gtk.Builder

Original `gtk_builder_add_from_file` and `gtk_builder_add_from_string`
return `guint` instead of `gboolean`, which would make direct usage
from Lua awkward.  Lgi overrides these methods to return `boolean` as
the first return value, so that typical
`assert(builder:add_from_file(filename))` can be used.

- `objects` attribute provides direct access to loaded objects by
  their identifier, so that instead of `builder:get_object('id')` it
  is possible to use `builder.objects.id`

- `new_from_file(filename)` new factory static method which creates
  new empty builder instance and calls `add_from_file()` on it.
  Constructed and initialized loader is then returned.

- `new_from_string(xml)` similar to `new_from_file()`, but instead of
  loading description from the file, gets description XML directly as
  a string argument.


See `samples/gtkbuilder.lua` in the Lgi source distribution for
typeical `Gtk.Builder` usage.
