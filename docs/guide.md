# LGI User's Guide

All LGI functionality is exported through `lgi` module.  To access it,
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
present in the library namespace - all classes, structs, global
functions, constants etc.  All those elements are directly accessible, e.g.

    assert(GLib.PRIORITY_DEFAULT == 0)

Note that all elements in the namespace are lazy-loaded to avoid
excessive memory overhead and initial loading time.  To force
eager-loading, all namespaces (and container elements in them, like
classes, structs, enums etc) contains `_resolve(deep)` method, which
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
* GObject class, struct or union is mapped to LGI instances of
  specific class, struct or union.  It is also possible to pass `nil`,
  in which case the `NULL` is passed to C-side (but only if the
  annotation `(allow-none)` of the original C method allows passing
  `NULL`).
* `gpointer` are mapped to Lua `lightuserdata` type.  In Lua->GLib
  direction, following values are accepted for `gpointer` type:
    - Lua `string` instances
    - Instances of LGI classes, structs or unions
    - Binary buffers (see below)

### 2.1. Modifiable binary buffers

Pure Lua lacks native binary modifiable buffer structure, which is a
problem for some GObject APIs, for example `Gio.InputStream.read()`,
which request pre-allocated buffer which will be modified (filled)
during the call.  To overcome this problem, LGI adopts the
[bytes proposal](http://permalink.gmane.org/gmane.comp.lang.lua.general/79288
"Defining a library for mutable byte arrays").  Since the standalone
implementation of this proposal does not seem to be available yet, LGI
uses its own implementation which is used when no external `bytes`
package can be found.  An example of `bytes` buffer usage follows:

    local lgi = require 'lgi'
    local bytes = require 'bytes'
    local Gio = lgi.Gio

    local stream = assert(Gio.File.new_for_path('foo.txt'):read())
    local buffer = bytes.new(50)
    local size = stream:read(buffer, #buffer)
    assert(size >= 0)
    print(tostring(buffer):sub(1, size))

Note that not full `bytes` proposal is currently implemented, 'Derived
operations' are not available except creating buffer from string using
`bytes.new` function.

### 2.2. Calling functions and methods

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

#### 2.2.1. Phantom boolean return values

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

To ease usage of such method, LGI avoids returning first boolean
return.  If C function returns `false` in this case, all other output
arguments are returned as `nil`.  This means that previous example
should be instead written simply as:

    local iter = model:get_iter_first()

### 2.3. Callbacks

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
asynchronous calls; see `samples\giostream.lua` for example usage of
this technique.

## 3. Classes

Classes are usually derived from `GObject` base class.  Classes
contain entities like properties, methods and signals and provide
inheritance, i.e. entities of ancestor class are also available in all
inherited classes.  LGI supports Lua-like access to entities using `.`
and `:` operators.

There is no need to invoke any memory management GObject controls,
like `ref` or `unref` methods, because LGI handles reference
management transparently underneath.  In fact, calling these low-level
methods can probably always be considered either as a bug or
workaround for possible bug in LGI :-)

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

### 3.2. Calling methods

Methods are functions grouped inside class (or interface)
declarations, accepting pointer to class instance as first argument.
Most usual technique to invoke method is using `:` operator,
e.g. `window:show_all()`.  This is of course identical with
`window.show_all(window)`, as is convention in plain Lua.

Method declaration itself is also avavilable in the class and it is
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
programmers, in LGI it might be preferrable to use `window =
Gtk.Window { type = Gtk.WindowType.TOPLEVEL }` instead of `window =
Gtk.Window.new(Gtk.WindowType.TOPLEVEL)`.

### 3.3. Accessing properties

Object properties are accessed simply by using `.` operator.
Continuing previous example, we can write `window.title = window.title
.. ' - new'`.  Note that in GObject system, property and signal names
can contain `-` character.  Since this character is illegal in Lua
identifiers, it is mapped to `_`, so `can-focus` window property is
accessed as `window.can_focus`.

### 3.4. Connecting signals

Signals are exposed as `on_signalname` entities on the class
instances.  Assigning Lua function connects that function to the
signal.  Signal routine gets object as first argument, followed by
other arguments of the signal. Simple example:

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

Reading signal entity provides temprary table which can be used for
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

### 3.5. Dynamic typing of classes

LGI assigns real class types to class instances dynamically, using
runtime GObject introspection facilities.  When new classes instance
is passed from C code into Lua, LGI queries the real type of the
object, finds the nearest type in the loaded repository and assigns
this type to the Lua-side created proxy for the object.  This means
that there is no casting needed in LGI (and there is also no casting
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
`GObject*`, LGI actually checks the real type of returned object and
assigns proper type to it, so `builder:get_object('window1')` returns
instance of `Gtk.Window` and `builder:get_object('action1')` returns
instance of `Gtk.Action`.

Another mechanism which allows complete lack of casting in LGI is
automatic interface discovery.  If some class implements some
interface, the properties and methods of the interface are directly
available on the class instance.

### 3.6. Accessing object's class instance

GObject has the notion of object class.  There are sometimes useful
methods defined on objects class, which are accessible to LGI using
object instance pseudo-property `class`.  For example, to list all
properties registered for object's class, GObject library provides
`g_object_class_list_properties()` function.  Following sample
lists all properties registered for the given object
instance.

    function dump_props(obj)
       print("Dumping properties of ", obj)
       for _, pspec in pairs(obj.class:list_properties()) do
	  print(pspec.name, pspec.value_type)
       end
    end

Running `dump_props(Gtk.Window())` yields following output:

    Dumping props of 	lgi.obj 0xe5c070:Gtk.Window(GtkWindow)
    name	gchararray
    parent	GtkContainer
    width-request	gint
    height-request	gint
    visible	gboolean
    sensitive	gboolean
    ... (and so on)

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
automatically mapped by LGI to the structure creation construct, so
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

Enum instances are represented as plain numbers in LGI.  So in any
place where enum or bitflags instance is needed, a number can be used
directly instead.

### 5.1. Accessing values

In order to retrieve realenum values from symbolic names, enum and
bitflags are loaded into repository as tables mapping symbolic names
to numeric constants.  Fro example, dumping `Gtk.WindowType` enum
yields following output:

> dump(Gtk.WindowType)

    ["table: 0xef9bc0"] = {  -- table: 0xef9bc0
      TOPLEVEL = 0;
      POPUP = 1;
    };

so constants can be referenced using `Gtk.WindowType.TOPLEVEL`
construct.

### 5.2. Backward mapping, getting names from numeric values

There is another facility in LGI, which allows backward mapping of
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

## 6. Threading and synchronization

Lua platform does not allow running real concurrent threads in single
Lua state.  This rules out any usage of GLib's threading API.
However, wrapped libraries can be using threads, and this can lead to
situations that callbacks or signals can be invoked from different
threads.  To avoid corruption which would result from running multiple
threads in a single Lua state, LGI implements one internal lock
(mutex) which protects access to Lua state.  LGI automatically locks
(i.e. waits on) this lock when performing C->Lua transition (invoking
Lua callback or returning from C call) and unlocks it on Lua->C
transition (returning from Lua callback or invoking C call).

In a typical GLib-based application, most of the runtime is spent
inside mainloop.  During this time, LGI lock is unlocked and mainloop
can invoke Lua callbacks and signals as needed.  This means that
LGI-based application does not have to worry about synchronization at
all.

The only situation which needs intervention is when mainloop is not
used or a different form of mainloop is used (e.g. Copas scheduler, Qt
UI etc).  In this case, LGI lock is locked almost all the time and
callbacks and signals are blocked and cannot be delivered.  To cope
with this situation, a `lgi.yield()` call exists.  This call
temporarily unlocks the LGI lock, letting other threads to deliver
waiting callbacks, and before returning the lock is closed back.  This
allows code which runs foreign, non-GLib style of mainloop to stick
`lgi.yield()` calls to some repeatedly invoked place and thus allowing
delivery of callbacks from other threads.

## 7. Logging

GLib provides generic logging facility using `g_message` and similar C
macros.  These utilities are not directly usable in Lua, so LGI
provides layer which allows logging messages using GLib logging
facilities and controlling behavior of logging methods.

All logging is controlled by `lgi.log` table.  To allow logging in
LGI-enabled code, `lgi.log.domain(name)` method exists.  This method
returns table containing methods `message`, `warning`, `critical`,
`error` and `debug` methods, which take format string optionally
followed by inserts and logs specifed string.  An example of typical
usage follows:

    local lgi = require 'lgi'
    local log = lgi.log.domain('myapp')

    -- This is equivalent of C 'g_message("A message %d", 1)'
    log.message("A message %d", 1)

    -- This is equivalent ot C 'g_warning("Not found")'
    log.warning("Not found")

Note that format string is formatted using Lua's `string.format()`, so
the rules for Lua formatting strings apply here.

## 8. GObject basic constructs

Although GObject library is already covered by gobject-introspection,
most of the elements in it are basic object system building blocks and
either need or greatly benefit from special handling by LGI.

### 8.1. GObject.Type

Contrary to C `GType` representation (which is unsigned number), LGI
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

### 8.2. GObject.Value

LGI does not implement any automatic `GValue` boxing or unboxing,
because this would involve guessing `GType` from Lua value, which is
generally unsafe.  Instead, an easy to use and convenient wrappers for
accessing `GValue` type and contents are provided.

#### 8.2.1. Creation

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

#### 8.2.2. Boxing and unboxing GObject.Value instances

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

### 8.3. GObject.Closure

Similar to GObject.Value, no automatic GClosure boxing is implemented.
To create a new instance of `GClosure`, 'call' closure type and
provide Lua function as an argument:

    closure = GObject.Closure(func)

When the closure is emitted, a Lua function is called, getting
`GObject.Value` as arguments and expecting to return `GObject.Value`
instance.
