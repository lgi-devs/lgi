# Gtk support

LGI Gtk support is based on gobject-introspection support.  Some
extensions are provided to support non-introspectable features and to
provide easier and more Lua-like access to some important Gtk
features.

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
by `property` attribute of `Gtk.Container`.  Following example
illustrates writing and reading of `x-padding` property of `Gtk.Grid`
and child `Gtk.Button`:

    local grid, button = Gtk.Grid(), Gtk.Button()
    grid:add(button)
    grid.property[button].width = 2
    print(grid.property[button].width)   -- prints 2

### Adding children to container

Basic method for adding child widget into container is
`Gtk.Container.add()` method.  This method is overloaded by LGI so
that it accepts either widget, or table containing widget at index 1
and the rest `name=value` pairs define child properties.  Therefore
this method is full replacement of unintrospectable
`gtk_container_add_with_properties()` function.  Example from previous
chapter simplified using this techinque follows:

    local grid, button = Gtk.Grid(), Gtk.Button()
    grid:add { button, width = 2 }
    print(grid.property[button].width)    -- prints 2

Another important feature of containers is that they have extended
constructor, and array part of constructor argument table can contain
widgets to be added.  Therefore, previous example can be written like
this:

    local button = Gtk.Button()
    local grid = Gtk.Grid {
       { button, width = 2 }
    }
    print(grid.property[button].width)    -- prints 2

### 'id' property of widgets

Another important feature is that all widgets support `id` property,
which can hold an arbitrary string which is used to identify the
widget.  `id` is assigned by caller, defaults to `nil`.  To lookup
widget with specified id in the container's widget tree (i.e. not only
in direct container children), query `child` property of the container
with requested id.  Previous example rewritten with this techinque
would look like this:

    local grid = Gtk.Grid {
       { Gtk.Button { id = 'button' }, width = 2 }
    }
    print(grid.property[grid.child.button].width)    -- prints 2

The advantage of these features is that they allow using Lua's
data-description face for describing widget hierarchies in natural
way, instead of human-unfriendly `Gtk.Builder`'s XML.  A small example
follows:

    Gtk = lgi.Gtk
    local window = Gtk.Window {
       title = 'Application',
       default_width = 640, default_height = 480,
       Gtk.Grid {
          orientation = Gtk.Orientation.VERTICAL,
          Gtk.Toolbar {
             Gtk.ToolButton { id = 'about', stock_id = Gtk.STOCK_ABOUT },
             Gtk.ToolButton { id = 'quit', stock_id = Gtk.STOCK_QUIT },
          },
          Gtk.ScrolledWindow {
             Gtk.TextView { id = 'view', expand = true }
          },
          Gtk.Statusbar { id = 'statusbar' }
       }
    }

    local n = 0    
    function window.child.about:on_clicked()
       n = n + 1
       window.child.view.buffer.text = 'Clicked ' .. n .. ' times'
    end

    function window.child.quit:on_clicked()
       window:destroy()
    end

    window:show_all()

Run `samples/console.lua`, paste example into entry view and enjoy.
The `samples/console.lua` example itself shows more complex usage of
this pattern.

Note: the `id` property is implemented by piggybacking on
`Gtk.Buildable.set_name` and `Gtk.Buildable.get_name` methods
(essentially, reading and writing `id` just calls these methods).

## Gtk.Builder

Although Lua's declarative style for creating widget hierarchies is
generally preferred to builder's XML authoring by hand, `Gtk.Builder`
can still be useful when widget hierarchies are designed in some
external tool like `glade`.

Original `gtk_builder_add_from_file` and `gtk_builder_add_from_string`
return `guint` instead of `gboolean`, which would make direct usage
from Lua awkward.  Lgi overrides these methods to return `boolean` as
the first return value, so that typical
`assert(builder:add_from_file(filename))` can be used.

- `objects` attribute provides direct access to loaded objects by
  their identifier, so that instead of `builder:get_object('id')` it
  is possible to use `builder.objects.id`

See `samples/gtkbuilder.lua` in the Lgi source distribution for
typical `Gtk.Builder` usage.
