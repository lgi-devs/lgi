# LGI Core Reference

## Core

Core LGI functionality is accessible through `lgi` module, loaded by
`require 'lgi'` command.  LGI does not install itself into global
namespace, caller has to use the return value from `require` call.

- `lgi.'module'`
    - `module` string with module name, e.g.'Gtk' or 'WebKit'.

  Loads requested module of the latest version found into the repository.

- `lgi.require(module, version)`
    - `module` string with module name, e.g. 'Gtk' or 'WebKit'.
    - `version` string with exact required version of the module

  Loads requested module with specified version into the repository.

- `lgi.log.domain(name)`
    - `name` is string denoting logging area name, usually identifying
      the application or the library
    - `return` table containing
	- `message`
	- `warning`
	- `critical`
	- `error`
	- `debug`

      methods for logging messages.  These methods accept format
      string and inserts, which are formatted according to Lua's
      `string.format` conventions.

- `lgi.yield()` when called, unlocks LGI state lock, for a while, thus
  allowing potentially blocked callbacks or signals to enter the Lua
  state.  When using LGI with GLib's MainLoop, this call is not needed
  at all.

## GObject basic constructs

### GObject.Type

- `NONE`, `INTERFACE`, `CHAR`, `UCHAR`, `BOOLEAN`,
  `INT`, `UINT`, `LONG`, `ULONG`, `INT64`, `UINT64`,
  `ENUM`, `FLAGS`, `FLOAT`, `DOUBLE`, `STRING`,
  `POINTER`, `BOXED`, `PARAM`, `OBJECT`, `VARIANT`
  
  Constants containing type names of fundamental GObject types.

- `parent`, `depth`, `next_base`, `is_a`, `children`, `interfaces`,
  `query`, `fundamental_next`, `fundamental`
  
  Functions for manipulating and querying `GType`.  THey are direct
  mappings of `g_type_xxx()` APIs, e.g. `GObject.Type.parent()`
  behaves in the same way as `g_type_parent()` in C.

### GObject.Value

- `GObject.Value([gtype [, val]])`
    - `gtype` type of the vlue to create, if not specified, defaults
      to `GObject.Type.NONE`.
    - `val` Lua value to initialize GValue with.

  Creates new GObject.Value of specified type, optionally assigns
  Lua value to it.  For example, `local val =
  GObject.Value(GObject.Type.INT, 42)` creates GValue of type
  `G_TYPE_INT` and initializes it to value `42`.

- `GObject.Value.gtype`
    - reading yields the gtype of the value
    - writing changes the type of the value.  Note that if GValue is
      already initialized with some value, a `g_value_transform` is
      called to attempt to convert value to target type.

- `GObject.Value.value`
    - reading retrieves Lua-native contents of the referenced Value
      (i.e. GValue unboxing is performed).
    - writing stores Lua-native contents to the Value (boxing is
      performed).

### GObject.Closure

- `GObject.Glosure(func)`
    - `target` is Lua function or anything Lua-callable.

  Creates new GClosure instance wrapping given Lua callable.  When
  the closure is emitted, `target` function is invoked, getting
  GObject.Value instances as arguments, and expecting single
  GObject.Value to be returned.
