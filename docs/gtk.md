# Gtk support

Lgi Gtk support is based on gobject-introspection support.  Some
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

### Gtk.Widget width and height properties

lgi adds new `width` and `height` properties to Gtk.Widget.  Reading them
yields allocated size (`Gtk.Widget.get_allocated_size()`), writing them sets
new size request (`Gtk.Widget.set_size_request()`).  These usages typically
means what an application needs - actual allocated size to draw on when
reading, and request for specific size when writing them.

### Child properties

Child properties are properties of the relation between a container
and child.  A Lua-friendly access to these properties is implemented
by `property` attribute of `Gtk.Container`.  Following example
illustrates writing and reading of `width` property of `Gtk.Grid`
and child `Gtk.Button`:

    local grid, button = Gtk.Grid(), Gtk.Button()
    grid:add(button)
    grid.property[button].width = 2
    print(grid.property[button].width)   -- prints 2

### Adding children to container

Basic method for adding child widget into container is
`Gtk.Container.add()` method.  This method is overloaded by Lgi so
that it accepts either widget, or table containing widget at index 1
and the rest `name=value` pairs define child properties.  Therefore
this method is full replacement of unintrospectable
`gtk_container_add_with_properties()` function.  Example from previous
chapter simplified using this technique follows:

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
widget.  `id` is assigned by caller, defaults to `nil`.  To look up
widget with specified id in the container's widget tree (i.e. not only
in direct container children), query `child` property of the container
with requested id.  Previous example rewritten with this technique
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

## Gtk.Builder

Although Lua's declarative style for creating widget hierarchies (as
presented in chapter discussing `Gtk.Container` extensions) is generally
preferred to builder's XML authoring by hand, `Gtk.Builder` can still be
useful when widget hierarchies are designed in some external tool like
`glade`.

Original `gtk_builder_add_from_file` and `gtk_builder_add_from_string`
return `guint` instead of `gboolean`, which would make direct usage
from Lua awkward.  Lgi overrides these methods to return `boolean` as
the first return value, so that typical
`assert(builder:add_from_file(filename))` can be used.

A new `objects` attribute provides direct access to loaded objects by
their identifier, so that instead of `builder:get_object('id')` it
is possible to use `builder.objects.id`

`Gtk.Builder.connect_signals(handlers)` tries to connect all signals
to handlers which are defined in `handlers` table.  Functions from
`handlers` table are invoked with target object on which is signal
defined as first argument, but it is possible to define `object`
attribute, in this case the object instance specified in `object`
attribute is used.  `after` attribute is honored, but `swapped` is
completely ignored, as its semantics for lgi is unclear and not very
useful.

## Gtk.Action and Gtk.ActionGroup

Lgi provides new method `Gtk.ActionGroup:add()` which generally replaces
unintrospectable `gtk_action_group_add_actions()` family of functions.
`Gtk.ActionGroup:add()` accepts single argument, which may be one of:

- an instance of `Gtk.Action` - this is identical with calling
  `Gtk.Action.add_action()`.
- a table containing instance of `Gtk.Action` at index 1, and
  optionally having attribute `accelerator`; this is a shorthand for
  `Gtk.ActionGroup.add_action_with_accel()`
- a table with array of `Gtk.RadioAction` instances, and optionally
  `on_change` attribute containing function to be called when the radio
  group state is changed.

All actions or groups can be added by an array part of `Gtk.ActionGroup`
constructor, as demonstrated by following example:

    local group = Gtk.ActionGroup {
       Gtk.Action { name = 'new', label = "_New" },
       { Gtk.Action { name = 'open', label = "_Open" },
         accelerator = '<control>O' },
       {
          Gtk.RadioAction { name = 'simple', label = "_Simple", value = 1 },
          { Gtk.RadioAction { name = 'complex', label = "_Complex",
            value = 2 }, accelerator = '<control>C' },
          on_change = function(action)
             print("Changed to: ", action.name)
          end
       },
    }

To access specific action from the group, a read-only attribute `action`
is added to the group, which allows to be indexed by action name to
retrieve.  So continuing the example above, we can implement 'new'
action like this:

    function group.action.new:on_activate()
       print("Action 'New' invoked")
    end

## Gtk.TextTagTable

It is possible to populate new instance of the tag table with tags
during the construction, an array part of constructor argument table is
expected to contain `Gtk.TextTag` instances which are then automatically
added to the table.

A new attribute `tag` is added, provides Lua table which can be indexed
by string representing tag name and returns the appropriate tag (so it is
essentially a wrapper around `Gtk.TextTagTable:lookup()` method).

Following example demonstrates both capabilities:

    local tag_table = Gtk.TextTagTable {
       Gtk.TextTag { name = 'plain', color = 'blue' },
       Gtk.TextTag { name = 'error', color = 'red' },
    }

    assert(tag_table.tag.plain == tag_table:lookup('plain'))

## TreeView and related classes

`Gtk.TreeView` and related classes like `Gtk.TreeModel` are one of the
most complicated objects in the whole `Gtk`.  Lgi adds some overrides
to simplify the work with them.

### Gtk.TreeModel

Lgi supports direct indexing of treemodel instances by iterators
(i.e. `Gtk.TreeIter` instances).  To get value at specified column
number, index the resulting value again with column number.  Note that
although `Gtk` uses 0-based column numbers, Lgi remaps them to 1-based
numbers, because working with 1-based arrays is much more natural for
Lua.

Another extension provided by Lgi is
`Gtk.TreeModel:pairs([parent_iter])` method for Lua-native iteration of
the model.  This method returns 3 values suitable to pass to generic
`for`, so that standard Lua iteration protocol can be used.  See the
example in the next chapter which uses this technique.

### Gtk.ListStore and Gtk.TreeStore

Standard `Gtk.TreeModel` implementations, `Gtk.ListStore` and
`Gtk.TreeStore` extend the concept of indexing model instance with
iterators also to writing values.  Indexing resulting value with
1-based column number allows writing individual values, while
assigning the table containing column-keyed values allows assigning
multiple values at once.  Following example illustrates all these
techniques:

    local PersonColumn = { NAME = 1, AGE = 2, EMPLOYEE = 3 }
    local store = Gtk.ListStore.new {
       [PersonColumn.NAME] = GObject.Type.STRING,
       [PersonColumn.AGE] = GObject.Type.INT,
       [PersonColumn.EMPLOYEE] = GObject.Type.BOOLEAN,
    }
    local person = store:append()
    store[person] = {
       [PersonColumn.NAME] = "John Doe",
       [PersonColumn.AGE] = 45,
       [PersonColumn.EMPLOYEE] = true,
    }
    assert(store[person][PersonColumn.AGE] == 45)
    store[person][PersonColumn.AGE] = 42
    assert(store[person][PersonColumn.AGE] == 42)

    -- Print all persons in the store
    for i, p in store:pairs() do
       print(p[PersonColumn.NAME], p[PersonColumn.AGE])
    end

Note that `append` and `insert` methods are overridden and accept
additional parameter containing table with column/value pairs, so
creation section of previous example can be simplified to:

    local person = store:append {
       [PersonColumn.NAME] = "John Doe",
       [PersonColumn.AGE] = 45,
       [PersonColumn.EMPLOYEE] = true,
    }

Note that while the example uses `Gtk.ListStore`, similar overrides
are provided also for `Gtk.TreeStore`.

### Gtk.TreeView and Gtk.TreeViewColumn

Lgi provides `Gtk.TreeViewColumn:set(cell, data)` method, which allows
assigning either a set of `cell` renderer attribute->model column
pairs (in case that `data` argument is a table), or assigns custom
data function for specified cell renderer (when `data` is a function).
Note that column must already have assigned cell renderer.  See
`gtk_tree_view_column_set_attributes()` and
`gtk_tree_view_column_set_cell_data_func()` for precise documentation.

The override `Gtk.TreeViewColumn:add(def)` composes both adding new
cellrenderer and setting attributes or data function.  `def` argument
is a table, containing cell renderer instance at index 1 and `data` at
index 2.  Optionally, it can also contain `expand` attribute (set to
`true` or `false`) and `align` (set either to `start` or `end`).  This
method is basically combination of `gtk_tree_view_column_pack_start()`
or `gtk_tree_view_column_pack_end()` and `set()` override method.

Array part of `Gtk.TreeViewColumn` constructor call is mapped to call
`Gtk.TreeViewColumn:add()` method, and array part of `Gtk.TreeView`
constructor call is mapped to call `Gtk.TreeView:append_column()`, and
this allows composing the whole initialized treeview in a declarative
style like in the example below:

    -- This example reuses 'store' model created in examples in
    -- Gtk.TreeModel chapter.
    local view = Gtk.TreeView {
       model = store,
       Gtk.TreeViewColumn {
          title = "Name and age",
          expand = true,
          { Gtk.CellRendererText {}, { text = PersonColumn.NAME } },
          { Gtk.CellRendererText {}, { text = PersonColumn.AGE } },
       },
       Gtk.TreeViewColumn {
          title = "Employee",
          { Gtk.CellRendererToggle {}, { active = PersonColumn.EMPLOYEE } }
       },
    }
