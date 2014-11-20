# lgi User's Guide

All lgi functionality is exported through `lgi` module.  To access it,
use standard `require` construct, e.g.:

    local lgi = require 'lgi'

Note that lgi does not use `module` function, so it does *not*
automatically insert itself into globals, the return value from
`require` call has to be used.

## 1. Importing libraries

To use any introspection-enabled library, it has to be imported first.
Simple way to import it is just referencing its name in `lgi`
namespace, like this:

    local GLib = lgi.GLib
    local GObject = lgi.GObject
    local Gtk = lgi.Gtk

This imports the latest version of the module which can be found.
When exact version is requested, use `lgi.require(modulename,
version)`:

    local Gst = lgi.require('Gst', '0.10')

### 1.1. Repository structure

Importing library creates table containing all elements which are
present in the library namespace - all classes, structures, global
functions, constants etc.  All those elements are directly accessible, e.g.

    assert(GLib.PRIORITY_DEFAULT == 0)

Note that all elements in the namespace are lazy-loaded to avoid
excessive memory overhead and initial loading time.  To force
eager-loading, all namespaces (and container elements in them, like
classes, structures, enums etc) contains `_resolve(deep)` method, which
loads all contents eagerly, possibly recursively if `deep` argument is
`true`.  So e.g.

    dump(Gtk.Widget:_resolve(true), 3)

prints everything available in Gtk.Widget class, and

    dump(Gio:_resolve(true), 5)

dumps the whole contents of Gio package.

Note: the `dump` function used in this manual is part of
`cli-debugger` Lua package.  Of course, you can use any kind of
table-dumping facility you are used to instead.

## 2. Mapping of types between GLib and Lua

In order to call methods and access properties and fields from Lua, a
mapping between GLib types and Lua types is established.

* `void` is ignored, does not produce any Lua value
* `gboolean` is mapped to Lua's `boolean` type, with `true` and
  `false` values
* All numeric types are mapped to Lua's `number` type
* Enumerations are primarily handled as strings with uppercased GType
  nicks, optionally the direct numeric values are also accepted.
* Bitflags are primarily handled as lists or sets of strings with
  uppercased GType nicks, optionally the direct numeric values are
  also accepted.
* `gchar*` string is mapped to Lua as `string` type, UTF-8 encoded
* C array types and `GArray` is mapped to Lua tables, using array part
  of the table.  Note that although in C the arrays are 0-based, when
  copied to Lua table, they are 1-based (as Lua uses 1-based arrays).
* `GList` and `GSList` is also mapped to Lua array part of tables.
* `GHashTable` is mapped to Lua table, fully utilizing key-value and
  GHashTable's key and value pairs.
* C arrays of 1-byte-sized elements (i.e. byte buffers) is mapped to
  Lua `string` instead of tables, although when going Lua->GLib
  direction, tables are also accepted for this type of arrays.
* GObject class, struct or union is mapped to lgi instances of
  specific class, struct or union.  It is also possible to pass `nil`,
  in which case the `NULL` is passed to C-side (but only if the
  annotation `(allow-none)` of the original C method allows passing
  `NULL`).
* `gpointer` are mapped to Lua `lightuserdata` type.  In Lua->GLib
  direction, following values are accepted for `gpointer` type:
    - Lua `string` instances
    - Instances of lgi classes, structs or unions
    - Binary buffers (see below)

### 2.1. Calling functions and methods

When calling GLib functions, following conventions apply:

* All input arguments are mapped to Lua inputs
* Return value is the first Lua return value
* All output arguments follow the return value
* In/Out arguments are both accepted as input and are also added into
  Lua returns.
* Functions reporting errors through `GError **` as last argument use
  Lua standard error reporting - they typically return boolean value
  indicating either success or failure, and if failure occurs,
  following return values represent error message and error code.

#### 2.1.1. Phantom boolean return values

GLib based libraries often use boolean return value indicating whether
logically-output argument is filled in or not.  Typical example is
`gboolean gtk_tree_model_get_iter_first(GtkTreeModel *tree_model,
GtkTreeIter *iter)`, where `iter` is filled in case of success, and
untouched in case of failure.  Normal binding of such function feels a
bit unnatural in Lua:

    local ok, iter = model:get_iter_first()
    -- Even in case of failure, iter contains new 0-initialized
    -- instance of the iterator, so following line is necessary:
    if not ok then iter = nil end

To ease usage of such method, lgi avoids returning first boolean
return.  If C function returns `false` in this case, all other output
arguments are returned as `nil`.  This means that previous example
must be instead written simply as:

    local iter = model:get_iter_first()

### 2.2. Callbacks

When some GLib function or method requires callback argument, a Lua
function should be provided (or userdata or table implementing
`__call` metamethod), and position for callback context (usually
called `user_data` in GLib function signature) should be ignored
completely.  Callbacks are invoked in the context of Lua coroutine
which invoked the original call, unless the coroutine is suspended -
in this, case a new coroutine is automatically created and callback is
invoked in this new context.

Callback arguments and return values are governed by the same rules of
argument and type conversions as written above.  If the Lua callback
throws an error, the error is *not* caught by the calling site,
instead propagated out (usually terminating unless there is some
`pcall` in the call chain).

It is also possible to provide coroutine instance as callback
argument.  In this case, the coroutine is resumed, providing callback
arguments as parameters to resume (therefore they are return values of
`coroutine.yield()` call which suspended the coroutine passed as
callback argument).  The callback is in this case considered to return
when either coroutine terminates (in this case, callback return
value(s) are coroutine final result(s)) or yields again (in this case,
callback return value(s) are arguments to `coroutine.yield()` call).
This mode of operation is very useful when using Gio-style
asynchronous calls.  However, it is recommended to use lgi-specific
`Gio.Async` facility for this purpose, as described in its own
documentation, because it wraps and hides many intricacies which arise
with coroutines and mainloop integration.

## 3. Classes

Classes are usually derived from `GObject` base class.  Classes
contain entities like properties, methods and signals and provide
inheritance, i.e. entities of ancestor class are also available in all
inherited classes.  lgi supports Lua-like access to entities using `.`
and `:` operators.

There is no need to invoke any memory management GObject controls,
like `ref` or `unref` methods, because lgi handles reference
management transparently underneath.  In fact, calling these low-level
methods can probably always be considered either as a bug or
workaround for possible bug in lgi :-)

### 3.1. Creating instances

To create new instance of the class (i.e. new object), call class
declaration as if it is a method:

    local window = Gtk.Window()

Optionally, it is possible to pass single argument, table containing
entity name mapping to entity value.  This way it is possible to
initialize properties, fields and even signal handlers in the class
construction:

    local window = Gtk.Window {
       title = "Title",
       on_destroy = function() print("Destroyed") end
    }

For some classes, which behave like containers of other things, lgi
allows adding also a list of children into the array part of the
argument table, which contains children element to be added.  A
typical example is `Gtk.Container`, which allows adding element in the
constructor table, allowing construction of the whole widget
hierarchy in Lua-friendly way:

    local window = Gtk.Window {
       title = "Title",
       on_destroy = function() print("Destroyed") end,
       Gtk.Grid {
          Gtk.Label { label = "Contents", expand = true },
          Gtk.Statusbar {}
       }
    }

There is also possibility to create instances of classes for which the
introspection typelib data is not available, only GType is known.  Use
`GObject.Object.new()` as illustrated in following sample:

    local gtype = 'ForeignWidget'
    local widget = GObject.Object.new(gtype)
    local window = Gtk.Window { title = 'foreign', widget }

### 3.2. Calling methods

Methods are functions grouped inside class (or interface)
declarations, accepting pointer to class instance as first argument.
Most usual technique to invoke method is using `:` operator,
e.g. `window:show_all()`.  This is of course identical with
`window.show_all(window)`, as is convention in plain Lua.

Method declaration itself is also available in the class and it is
possible to invoke it without object notation, so previous example can
be also rewritten as `Gtk.Window.show_all(window)`.  Note that this
way of invoking removes dynamic lookup of the method from the object
instance type, so it might be marginally faster.  However, in case
that `window` is actually instance of some `GtkWindow` descendant,
lets say `MyWindow`, which also defined `my_window_show_all()` method,
there will be a difference: `window:show_all()` will invoke
`my_window_show_all(window)`, while `Gtk.Window.show_all(window)` will
of course invoke non-specialized `gtk_widget_show_all(window)`.

#### 3.2.1. Static methods

Static methods (i.e. functions which do not take class instance as
first argument) are usually invoked using class namespace,
e.g. `Gtk.Window.list_toplevels()`.  Very common form of static
methods are `new` constructors, e.g. `Gtk.Window.new()`.  Note that in
most cases, `new` constructors are provided only as convenience for C
programmers, in lgi it might be preferable to use `window =
Gtk.Window { type = Gtk.WindowType.TOPLEVEL }` instead of `window =
Gtk.Window.new(Gtk.WindowType.TOPLEVEL)`.

### 3.3. Accessing properties

Object properties are accessed simply by using `.` operator.
Continuing previous example, we can write `window.title = window.title
.. ' - new'`.  Note that in GObject system, property and signal names
can contain `-` character.  Since this character is illegal in Lua
identifiers, it is mapped to `_`, so `can-focus` window property is
accessed as `window.can_focus`.

### 3.4. Signals

Signals are exposed as `on_signalname` entities on the class
instances.

#### 3.4.1. Connecting signals

Assigning Lua function connects that function to the signal.  Signal
routine gets object as first argument, followed by other arguments of
the signal. Simple example:

    local window = Gtk.Window()
    window.on_destroy = function(w)
       assert(w == window)
       print("Destroyed", w)
    end

Note that because of Lua's syntactic sugar for object access and
function definition, it is possible to use signal connection even in
following way:

    local window = Gtk.Window()
    function window:on_destroy()
       assert(self == window)
       print("Destroyed", self)
    end

Reading signal entity provides temporary table which can be used for
connecting signal with specification of the signal detail (see GObject
documentation on signal detail explanation).  An example of handler
which is notified whenever window is activated or deactivated follows:

    local window = Gtk.Window()
    window.on_notify['is-active'] = function(self, pspec)
       assert(self == window)
       assert(pspec.name == 'is-active')
       print("Window is active:", self.is_active)
    end

Both forms of signal connection connect handler before default signal
handler.  If connection after default signal handler is wanted (see
`G_CONNECT_AFTER` documentation for details), the most generic
connection call has to be used: `object.on_signalname:connect(target,
detail, after)`.  Previous example rewritten using this connection
style follows:

    local window = Gtk.Window()
    local function notify_handler(self, pspec)
       assert(self == window)
       assert(pspec.name == 'is-active')
       print("Window is active:", self.is_active)
    end
    window.on_notify:connect(notify_handler, 'is-active', false)

#### 3.4.2 Emitting signals

Emitting existing signals is usually needed only when implementing
subclasses of existing classes.  Simplest method to emit a signal is
to 'call' the signal on the class instance:

    local treemodel = <subclass implementing Gtk.TreeModel>
    treemodel:on_row_inserted(path, iter)

### 3.5. Dynamic typing of classes

lgi assigns real class types to class instances dynamically, using
runtime GObject introspection facilities.  When new classes instance
is passed from C code into Lua, lgi queries the real type of the
object, finds the nearest type in the loaded repository and assigns
this type to the Lua-side created proxy for the object.  This means
that there is no casting needed in lgi (and there is also no casting
facility available).

Hopefully everything can be explained in following example.  Assume
that `demo.ui` is GtkBuilder file containing definition of `GtkWindow`
labeled `window1` and `GtkAction` called `action1` (among others).

    local builder = Gtk.Builder()
    builder:add_from_file('demo.ui')
    local window = builder:get_object('window1')
    -- Call Gtk.Window-specific method
    window:iconify()
    local action = builder:get_object('action1')
    -- Set Gtk.Action-specific property
    action.sensitive = false

Although `Gtk.Builder.get_object()` method is marked as returning
`GObject*`, lgi actually checks the real type of returned object and
assigns proper type to it, so `builder:get_object('window1')` returns
instance of `Gtk.Window` and `builder:get_object('action1')` returns
instance of `Gtk.Action`.

Another mechanism which allows complete lack of casting in lgi is
automatic interface discovery.  If some class implements some
interface, the properties and methods of the interface are directly
available on the class instance.

### 3.6. Accessing object's class instance

GObject has the notion of object class.  There are sometimes useful
methods defined on objects class, which are accessible to lgi using
object instance pseudo-property `_class`.  For example, to list all
properties registered for object's class, GObject library provides
`g_object_class_list_properties()` function.  Following sample lists
all properties registered for the given object instance.

    function dump_props(obj)
       print("Dumping properties of ", obj)
       for _, pspec in pairs(obj._class:list_properties()) do
          print(pspec.name, pspec.value_type)
       end
    end

Running `dump_props(Gtk.Window())` yields following output:

    Dumping props of	lgi.obj 0xe5c070:Gtk.Window(GtkWindow)
    name	gchararray
    parent	GtkContainer
    width-request	gint
    height-request	gint
    visible	gboolean
    sensitive	gboolean
    ... (and so on)

### 3.7. Querying the type of the object instances

To query whether given Lua value is actually an instance of specified
class or subclass, class types define `is_type_of` method.  This
class-method takes one argument and checks, whether given argument as an
instance of specified class (or implements specified interface, when
called on interface instances).  Following examples demonstrate usage of
this construct:

    local window = Gtk.Window()
    print(Gtk.Window:is_type_of(window))    -- prints 'true'
    print(Gtk.Widget:is_type_of(window))    -- prints 'true'
    print(Gtk.Buildable:is_type_of(window)) -- prints 'true'
    print(Gtk.Action:is_type_of(window))    -- prints 'false'
    print(Gtk.Window:is_type_of('string'))  -- prints 'false'
    print(Gtk.Window:is_type_of(nil))       -- prints 'false'

There is also possibility to query the type-table from instantiated
object, using `_type` property.

    -- Checks, whether 'unknown' conforms to the type of the 'template'
    -- object.
    function same_type(template, unknown)
       local type = template._type
       return type:is_type_of(unknown)
    end

### 3.8. Implementing subclasses

It is possible to implement subclass of any existing class in pure
Lua.  The reason to do so is to implement virtual methods of parent
class (and possibly one or more interfaces).

In order to create subclass, lgi requires to create `package` first,
which is basically namespace where the new classes will live.  To
create a package, use `lgi.package` function:

    -- Create MyApp package
    local MyApp = lgi.package 'MyApp'

Once the package is created, it is possible to reference it from `lgi`
as any other existing namespace:

    local Gtk = lgi.Gtk
    local MyApp = lgi.MyApp

To create subclass, use package's method `class(name, parent[, ifacelist])`:

    MyApp:class('MyWidget', Gtk.Widget)
    MyApp:class('MyModel', GObject.Object, { Gtk.TreeModel })

After that, newly created class behaves exactly the same as classes
picked up from GObjectIntrospection namespaces, like shown in
following examples:

    local widget = MyApp.MyWidget()
    widget:show()

Note that it is important to override virtual methods _before_ any
instance of the derived class (see chapter about virtual methods
below).

### 3.8.1. Overriding virtual methods

To make subclass useful, it is needed to override some of its virtual
methods.  Existing virtual methods are prefixed with `do_`.  In order
to call inherited virtual methods, it is needed to use an explicit
function reference.  There is an automatic property called `priv`
which is plain Lua table and allows subclass implementation to store
some internal status.  All these techniques are illustrated in
following sample:

    function MyApp.MyWidget:do_show()
       if not self.priv.invisible then
          -- All three lines perform forwarding to inherited virtual:
          Gtk.Widget.do_show(self)
          -- or:
          MyApp.MyWidget._parent.do_show(self)
          -- or:
          self._type._parent.do_show(self)
       end
    end

    -- Convenience method for setting MyWidget 'invisible'
    function MyApp.MyWidget:set_invisible(invisible)
       self.priv.invisible = invisible
    end

The important fact is that virtual method overrides are picked up only
up to the first instantiation of the class or inheriting new subclass
from it.  After this point, virtual function overrides are ignored.

### 3.8.2. Installing new properties

To add new property for derived class, a new `GObject.ParamSpec`
instance describing property must be added into `_property` table of
derived class.  This must be done before first instantiation of the class.

By default, the value of the property is mirrored in `priv` table of
the instance.  However, it is possible to specify custom getter and
setter method in `_property_set` and `_property_get` tables.  Both
approaches are illustrated in the following example with property
called `my_label`

    MyApp.MyWidget._property.my_label = GObject.ParamSpecString(
        'my_label', 'Nick string', 'Blurb string', 'def-value',
        { 'READABLE', 'WRITABLE', 'CONSTRUCT' })
    function MyApp.MyWidget._property_set:my_label(new_value)
        print(('%s changed my_label from %s to %s'):format(
	    self, self.priv-my_label, new_value))
        self.priv.my_label = new_value
    end
    local widget = MyApp.MyWidget()

    -- Access through GObject's property machinery
    widget.my_label = 'label1'
    print(widget.my_label)

    -- Direct access to underlying storage
    print(widget.priv.my_label)

## 4. Structures and unions

Structures and unions are supported in a very similar way to classes.
They have only access to methods (in the same way as classes) and
fields, which are very similar to the representation of properties on
the classes.

### 4.1. Creating instances

Structure instances are created by 'calling' structure definition,
similar to creating new class: `local color = Gdk.RGBA()`.  For
simplest structures without constructor methods, the new structure is
allocated and zero-initialized.  It is also possible to pass table
containing fields and values to which the fields should be
initialized: `local blue = Gdk.RGBA { blue = 1, alpha = 1 }`.

If the structure has defined any constructor named `new`, it is
automatically mapped by lgi to the structure creation construct, so
calling `local main_loop = GLib.MainLoop(nil, false)` is exactly
equivalent with `local main_loop = GLib.MainLoop.new(nil, false)`, and
`local color = Clutter.Color(0, 0, 0, 255)` is exactly equivalent with
`local color = Clutter.Color.new(0, 0, 0, 255)`.

### 4.2. Calling methods and accessing fields.

Structure methods are called in the same way as class methods:
`struct:method()`, or `StructType.method()`.  For example:

    local loop = GLib.MainLoop(nil, false)
    loop:run()
    -- Following line is equivalent
    GLib.MainLoop.run(loop)

Fields are accessed using `.` operator on structure instance, for example

    local color = Gdk.RGBA { alpha = 1 }
    color.green = 0.5
    print(color.red, color.green, color.alpha)
    -- Prints: 0    0.5    1

## 5. Enums and bitflags, constants

lgi primarily maps enumerations to strings containing uppercased nicks
of enumeration constant names.  Optionally, a direct enumeration value
is also accepted.  Similarly, bitflags are primarily handled as sets
containing uppercased flag nicks, but also lists of these nicks or
direct numeric value is accepted.  When a numeric value cannot be
mapped cleanly to the known set of bitflags, the remaining number is
stored in the first array slot of the returned set.

Note that this behavior changed in lgi 0.4; up to that alpha release,
lgi handled enums and bitmaps exclusively as numbers only.  The change
is compatible in Lua->C direction, where numbers still can be used,
but incompatible in C->Lua direction, where lgi used to return
numbers, while now it returns either string with enum value or table
with flags.

### 5.1. Accessing numeric values

In order to retrieve real enum values from symbolic names, enum and
bitflags are loaded into repository as tables mapping symbolic names
to numeric constants.  Fro example, dumping `Gtk.WindowType` enum
yields following output:

> dump(Gtk.WindowType)

    ["table: 0xef9bc0"] = {  -- table: 0xef9bc0
      TOPLEVEL = 0;
      POPUP = 1;
    };

so constants can be referenced using `Gtk.WindowType.TOPLEVEL`
construct, or directly using string `'TOPLEVEL'` when a
`Gtk.WindowType` is expected.

### 5.2. Backward mapping, getting names from numeric values

There is another facility in lgi, which allows backward mapping of
numeric constants to symbolic names.  Indexing enum table with number
actually provides symbolic name to which the specified constant maps:

> print(Gtk.WindowType[0])

    TOPLEVEL

> print(Gtk.WindowType[2])

    nil

Indexing bitflags table with number provides table containing list of
all symbolic names which make up the requested value:

> dump(Gtk.RegionFlags)

    ["table: 0xe5dd10"] = {  -- table: 0xe5dd10
      ODD = 2;
      EVEN = 1;
      SORTED = 32;
      FIRST = 4;
      LAST = 8;

> dump(Gtk.RegionFlags[34])

    ["table: 0xedbba0"] = {  -- table: 0xedbba0
      SORTED = 32;
      ODD = 2;
    };

This way, it is possible to check for presence of specified flag very
easily:

    if Gtk.RegionFlags[flags].ODD then
       -- Code handling region-odd case
    endif

If the value cannot be cleanly decomposed to known flags, remaining
bits are accumulated into number stored at index 1:

> dump(Gtk.RegionFlags[51])

    ["table: 0x242fb20"] = {  -- table: 0x242fb20
      EVEN = 1;
      SORTED = 32;
      [1] = 16;
      ODD = 2;
    };

To construct numeric value which can be passed to a function expecting
an enum, it is possible to simply add requested flags.  However, there
is a danger if some definition contains multiple flags , in which case
numeric adding produces incorrect results.  Therefore, it is possible
to use bitflags pseudoconstructor', which accepts table containing
requested flags:

> =Gtk.RegionFlags { 'FIRST', 'SORTED' }

    36

> =Gtk.RegionFlags { Gtk.RegionFlags.ODD, 16, 'EVEN' }

    19

## 6. Threading and synchronization

Lua platform does not allow running real concurrent threads in single
Lua state.  This rules out any usage of GLib's threading API.
However, wrapped libraries can be using threads, and this can lead to
situations that callbacks or signals can be invoked from different
threads.  To avoid corruption which would result from running multiple
threads in a single Lua state, lgi implements one internal lock
(mutex) which protects access to Lua state.  lgi automatically locks
(i.e. waits on) this lock when performing C->Lua transition (invoking
Lua callback or returning from C call) and unlocks it on Lua->C
transition (returning from Lua callback or invoking C call).

In a typical GLib-based application, most of the runtime is spent
inside mainloop.  During this time, lgi lock is unlocked and mainloop
can invoke Lua callbacks and signals as needed.  This means that
lgi-based application does not have to worry about synchronization at
all.

The only situation which needs intervention is when mainloop is not
used or a different form of mainloop is used (e.g. Copas scheduler, Qt
UI etc).  In this case, lgi lock is locked almost all the time and
callbacks and signals are blocked and cannot be delivered.  To cope
with this situation, a `lgi.yield()` call exists.  This call
temporarily unlocks the lgi lock, letting other threads to deliver
waiting callbacks, and before returning the lock is closed back.  This
allows code which runs foreign, non-GLib style of mainloop to stick
`lgi.yield()` calls to some repeatedly invoked place and thus allowing
delivery of callbacks from other threads.

## 7. Logging

GLib provides generic logging facility using `g_message` and similar C
macros.  These utilities are not directly usable in Lua, so lgi
provides layer which allows logging messages using GLib logging
facilities and controlling behavior of logging methods.

All logging is controlled by `lgi.log` table.  To allow logging in
lgi-enabled code, `lgi.log.domain(name)` method exists.  This method
returns table containing methods `message`, `warning`, `critical`,
`error` and `debug` methods, which take format string optionally
followed by inserts and logs specified string.  An example of typical
usage follows:

    local lgi = require 'lgi'
    local log = lgi.log.domain('myapp')

    -- This is equivalent of C 'g_message("A message %d", 1)'
    log.message("A message %d", 1)

    -- This is equivalent to C 'g_warning("Not found")'
    log.warning("Not found")

Note that format string is formatted using Lua's `string.format()`, so
the rules for Lua formatting strings apply here.

## 8. Interoperability with native code

There might be some scenarios where it is important to either export
objects or records created in Lua into C code or vice versa.  lgi
allows transfers using Lua `lightuserdata` type.  To get native
pointer to the lgi object, use `_native` attribute of the object.  To
create lgi object from external pointer, it is possible to pass
lightuserdata with object pointer to type constructor.  Following
example illustrates both techniques:

    -- Create Lua-side window object.
    local window = Gtk.Window { title = 'Hello' }
    
    -- Get native pointer to this object.
    local window_ptr = window._native
    
    // window_ptr can be now passed to C code, which can use it.
    GtkWindow *window = lua_touserdata (L, x);
    char *title;
    g_object_get (window, "title", &title);
    g_assert (g_str_equal (title, "Hello"));
    g_free (title);
    
    // Create object on the C side and pass it to Lua
    GtkButton *button = gtk_button_new_with_label ("Foreign");
    lua_pushlightuserdata (L, button);
    lua_call (L, ...);
    
    -- Retrieve button on the Lua side.
    assert(type(button) == 'userdata')
    window:add(Gtk.Button(button))

Note that while the example demonstrates objects, the same mechanism
works also for structures and unions.

## 9. GObject basic constructs

Although GObject library is already covered by gobject-introspection,
most of the elements in it are basic object system building blocks and
either need or greatly benefit from special handling by lgi.

### 9.1. GObject.Type

Contrary to C `GType` representation (which is unsigned number), lgi
represents GType by its name, as a string.  GType-related constants
and methods useful for handling GType are present in `GObject.Type`
namespace.

Fundamental GType names are imported as constants into GObject.Type
namespace, so that it is possible to use for example
`GObject.Type.INT` where `G_TYPE_INT` would be used in C code.
Following constants are available:

> `NONE`, `INTERFACE`, `CHAR`, `UCHAR`, `BOOLEAN`,
> `INT`, `UINT`, `LONG`, `ULONG`, `INT64`, `UINT64`,
> `ENUM`, `FLAGS`, `FLOAT`, `DOUBLE`, `STRING`,
> `POINTER`, `BOXED`, `PARAM`, `OBJECT`, `VARIANT`

Moreover, functions operating on `GType` are also present in
`GObject.Type` namespace:

> `parent`, `depth`, `next_base`, `is_a`, `children`, `interfaces`,
> `query`, `fundamental_next`, `fundamental`

There is special new method, `Type.type(gtype)` which returns lgi
native type representing specified gtype.  For example:

    assert(Gtk.Window == GObject.Type.type('GtkWindow'))
    assert(Gtk.WidgetPath == GObject.Type.type('GtkWidgetPath'))

When transferring `GType` value from Lua to C (e.g. calling function
which accepts argument of `GType`), it is possible to use either
string with type name, number representing numeric `GType` value, or
any loaded component which has its type assigned.  Some examples of
`GType` usage follow:

    lgi = require 'lgi'
    GObject = lgi.GObject
    Gtk = lgi.Gtk

    print(GObject.Type.NONE)
    print(GObject.Type.name(GObject.Type.NONE))
    -- prints "void" in both cases

    print(GObject.Type.name(Gtk.Window))
    -- prints "GtkWindow"

    print(GObject.Type.is_a(Gtk.Window, GObject.Type.OBJECT))
    -- prints "true"

    print(GObject.Type.parent(Gtk.Window))
    -- prints "GtkBin"

### 9.2. GObject.Value

lgi does not implement any automatic `GValue` boxing or unboxing,
because this would involve guessing `GType` from Lua value, which is
generally unsafe.  Instead, an easy to use and convenient wrappers for
accessing `GValue` type and contents are provided.

#### 9.2.1. Creation

To create new `GObject.Value` instances, use similar method as for
creating new structures or classes, i.e. 'call' `GObject.Value` type.
The call has two optional arguments, specifying `GType` of newly
created `GValue` and optionally also the contents of the value.  A few
examples for creating new values follow:

    local lgi = require 'lgi'
    local GObject = lgi.GObject
    local Gtk = lgi.Gtk

    local empty = GObject.Value()
    local answer = GObject.Value(GObject.Type.INT, 42)
    local null_window = GObject.Value(Gtk.Window)
    local window = GObject.Value(Gtk.Window, Gtk.Window())

#### 9.2.2. Boxing and unboxing GObject.Value instances

`GObject.Value` adds two new virtual properties, called `gtype` and
`value`.  `gtype` contains actual type of the value, while `value`
provides access to the contents of the value.  Both properties are
read/write.  Reading of them queries current `GObject.Value` state,
i.e. reading `value` performs actual `GValue` unboxing.  Writing
`value` performs value boxing, i.e. the source Lua item is attempted
to be stored into the `GObject.Value`.  Writing `gtype` attempts to
change the type of the value, and in case that value already has a
contents, it also converts contents to the new type (using
`g_value_transform()`).  Examples here continue using the values
created in previous section example:

    assert(empty.gtype == nil)
    assert(empty.value == nil)
    assert(answer.gtype == GObject.Type.INT)
    assert(answer.value == 42)
    assert(null_window.gtype == 'GtkWindow')
    assert(null_window.value == nil)

    empty.gtype = answer.gtype
    empty.value = 1
    assert(empty.gtype == GObject.Type.INT)
    assert(empty.value == 1)
    answer.gtype = GObject.Type.STRING)
    assert(answer.value == '42')

Although `GObject.Value` provides most of the GValue documented
methods (e.g. `g_value_get_string()` is accessible as
`GObject.Value.get_string()` and getting string contents of the value
instance can be written as `value:get_string()`), `value` and `gtype`
abstract properties are recommended to be used instead.

### 9.3. GObject.Closure

Similar to GObject.Value, no automatic GClosure boxing is implemented.
To create a new instance of `GClosure`, 'call' closure type and
provide Lua function as an argument:

    closure = GObject.Closure(func)

When the closure is emitted, a Lua function is called, getting
`GObject.Value` as arguments and expecting to return `GObject.Value`
instance.
