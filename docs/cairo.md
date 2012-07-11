# cairo support

Cairo library is an important part of any GTK-based setup, because GTK
internally uses cairo exclusively for painting.  However, cairo itself
is not built using GObject technology and thus imposes quite a problem
for any GObject Introspection based binding, such as lgi.

## Basic binding description

Although internal implementation is a bit different from other fully
introspection-enabled libraries, this difference is not visible to lgi
user.  cairo must be imported in the same way as other libraries, e.g.

    local lgi = require 'lgi'
    local cairo = lgi.cairo

Cairo library itself is organized using object-oriented style, using C
structures as objects (e.g. `cairo_t`, `cairo_surface_t`) and
functions as methods acting upon these objects.  lgi exports objects
as classes in the `cairo` namespace, e.g. `cairo.Context`,
`cairo.Surface` etc.  To create new object instance, cairo offers
assorted `create` methods, e.g. `cairo_create` or
`cairo_pattern_create`, which are mapped as expected to
`cairo.Context.create` and `cairo.Pattern.create`.  It is also
possible to invoke them using lgi's 'constructor' syntax, i.e. to
create new context on specified surface, it is possible to use either
`local cr = cairo.Context.create(surface)` or `local cr =
cairo.Context(surface)`.

### Version checking

`cairo.version` and `cairo.version_string` fields contain current
runtime cairo library version, as returned by their C counterparts
`cairo_version()` and `cairo_version_string()`.  Original
`CAIRO_VERSION_ENCODE` macro is reimplemented as
`cairo.version_encode(major, minor, micro)`.  For example, following
section shows how to guard code which should be run only when cairo
version is at least 1.12:

    if cairo.version >= cairo.version_encode(1, 12, 0) then
       -- Cairo 1.12-specific code
    else
       -- Fallback to older cairo version code
    end

### Synthetic properties

There are many getter and setter functions for assorted cairo objects.
lgi exports them in the form of method calls as the native C interface
does, and it also provides property-like access, so that it is
possible to query or assign named property of the object.  Following
example demonstrates two identical ways to set and get line width on
cairo.Context instance:

    local cr = cairo.Context(surface)
    cr:set_line_width(10)
    print('line width ', cr:get_line_width())

    cr.line_width = 10
    print('line width ', cr.line_width)

In general, any kind of `get_xxx()` method call on any cairo object
can be replaced using `xxx` property on the object, and any
`set_xxx()` method can be replaced by setting `xxx` property.

### cairo.Surface hierarchy

Cairo provides basic rendering surface object `cairo.Surface`, and a
bunch of specialized surfaces implementing rendering to assorted
targets, e.g. `cairo.ImageSurface`, `cairo.PdfSurface` etc.  These
surface provide their own class, which is logically inherited from
`cairo.Surface`.  lgi fully implements this inheritance, so that
calling `cairo.ImageSurface()` actually creates an instance of
`cairo.ImageSurface` class, which provides all methods abd properties
of `cairo.Surface` and and some specialized methods and properties
like `width` and `height`.

In addition, lgi always assigns the real type of the surface, so that
even when `cairo.Context.get_target()` method (or
`cairo.Context.target` property) is designated as returning
`cairo.Surface` instance, upon the call the type of the surface is
queried and proper kind of surface type is really returned.  Following
example demonstrates that it is possible to query
`cairo.ImageSurface`-specific `width` property directly on the
`cairo.Context.target` result.

    -- Assumes the cr is cairo.Context instance with assigned surface
    print('width of the surface' cr.target.width)

It is also possible to use lgi generic typechecking machinery for
checking the type of the surface:

    if cairo.ImageSurface:is_type_of(cr.target) then
	print('width of the surface' cr.target.width)
    else
	print('unsupported type of the surface')
    end

### cairo.Pattern hierarchy

cairo's pattern API actually hides the inheritance of assorted pattern
types.  lgi binding brings this hierarchy up in the same way as for
surfaces described in previous section.  Following hierarchy exists:

    cairo.Pattern
        cairo.SolidPattern
	cairo.SurfacePattern
	cairo.GradientPattern
	    cairo.LinearPattern
	    cairo.RadialPattern
	cairo.MeshPattern

Patterns can be created using static factory methods on
`cairo.Pattern` as documented in cairo documentation.  In addition,
lgi maps creation methods to specific subclass constructors, so
following snippets are equivalent:

    local pattern = cairo.Pattern.create_linear(0, 0, 10, 10)
    local pattern = cairo.LinearPattern(0, 0, 10, 10)

### cairo.Context path iteration

cairo library offers iteration over the drawing path returned via
`cairo.Context.copy_path()` method.  Resulting path can be iterated
using `pairs()` method of `cairo.Path` class.  `pairs()` method
returns iterator suitable to be used in Lua 'generic for' construct.
Iterator returns type of the path element, optionally followed by 0, 1
or 3 points.  Following example shows how to iterate the path.

    local path = cr:copy_path()
    for kind, points in path:pairs() do
       io.write(kind .. ':')
          for pt in ipairs(points) do
             io.write((' { %g, %g }'):format(pt.x, pt.y))
	  end
       end
    end

## Impact of cairo on other libraries

In addition to cairo itself, there is a bunch of cairo-specific
methods inside Gtk, Gdk and Pango libraries.  lgi wires them up so
that they can be called naturally as if they were built in to the
cairo core itself.

### Gdk and Gtk

`Gdk.Rectangle` is just a link to `cairo.RectangleInt` (similar to C,
where `GdkRectangle` is just a typedef of `cairo_rectangle_int_t`).
`gdk_rectangle_union` and `gdk_rectangle_intersect` are wired as a
methods of `Gdk.Rectangle` as expected.

`Gdk.cairo_create()` is aliased as a method
`Gdk.Window.cairo_create()`.  `Gdk.cairo_region_create_from_surface()`
is aliased as `cairo.Region.create_from_surface()`.

`cairo.Context.set_source_rgba()` is overriden so that it also accepts
`Gdk.RGBA` instance as an argument.  Similarly,
`cairo.Context.rectangle()` alternatively accepts `Gdk.Rectangle` as
an argument.

`cairo.Context` has additional methods `get_clip_rectangle()`,
`set_source_color()`, `set_source_pixbuf()`, `set_source_window` and
`region`, implemented as calls to appropriate `Gdk.cairo_xxx`
functions.

Since all these extensions are implemented inside Gdk and Gtk
libraries, they are present only when `lgi.Gdk` is loaded.  When
loading just pure `lgi.cairo`, they are not available.

### PangoCairo

Pango library contains namespace `PangoCairo` which implements a bunch
of cairo-specific helper functions to integrate Pango use with cairo
library.  It is of course possible to call them as global methods of
PangoCairo interface, however lgi override maps the also to methods
and attributes of other classes to which they logically belong.
