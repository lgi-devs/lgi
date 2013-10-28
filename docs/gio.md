# Gio support

Most of the Gio facilities are supported natively through
gobject-introspection layer.  However, lgi provides `Gio.Async` which
help in using Gio-style asynchronous I/O operations.

## Asynchronous IO support

Native Gio asynchronous operations are based on traditional callback
scheme, i.e. the operation is started, pushed to be performed on the
background and when it finishes, it calls registered callback with
operation results as arguments.  While this scheme is widely used for
asynchronous programming, it leads to spaghetti code full of
callbacks.  Lua provides coroutines, which can be used to make the
code look 'synchronous' again, but still retaining the advantage of
non-blocking I/O operations.

Gio-style asynchronous functions come as pair of two methods;
`name_async` which starts operation and registers callback, and
`name_finish`, which should be called in the context of registered
callback and allows retrieveing operation results.  When `Gio`
override is loaded, lgi detects any of these pairs (in any object, not
just from Gio namespace) and when found, it synthesizes `async_name`
operations, which wraps native methods and uses Lua coroutines to
convert callbacks into synchronous code.  In order for `async_method`
to work, these methods have to be called in the context of functions
called through `Gio.Async` spawning facilities; either
`Gio.Async.call` for synchronous calls and `Gio.Async.start` for
starting routine on background.

### Gio.Async class

This helper class implemented by lgi (not originating from
introspected Gio module) contains interface for using lgi asynchronous
support.  This class contains only static methods and attributes, it
is not possible to instantiate it.

### Gio.Async.call and Gio.Async.start

    local call_results = Gio.Async.call(user_function[, cancellable[, io_priority])(user_args)
    local resume_results = Gio.Async.start(user_function[, cancellable[, io_priority])(user_args)

These methods accept user function to be run as argument and return
function which starts execution of the user function in async-enabled
context.

Any `async_name` methods called inside context do not accept
`io_priority` and `cancellable` arguments (as their `name_async`
original counterparts do), instead global cancellable and io_priority
values given as arguments to `Gio.Async.call/start` are used.

### Gio.Async.cancellable and Gio.Async.io_priority

Code running inside async-enabled context can query or change value of
context-default `cancellable` and `io_priority` attributes by getting
or setting them as attributes of `Gio.Async` class.

If `cancellable` or `io_priority` arguments are not provided to
`Gio.Async.start` or `Gio.Async.call`, they are automatically
inherited from currently running async-enabled coroutine, or default
values are used (if caller is not running in async-enabled context).

### Simple asynchronous I/O example

Following example reacts on the press of button, reads contents of
`/etc/passwd` and dumps it to standard output.

    local window = Gtk.Window {
      ...   Gtk.Button { id = 'button', label = 'Breach' }, ...
    }
    
    function window.child.button:on_clicked()
        local function dump_file(filename)
            local file = Gio.File.new_for_path(filename)
            local info = file:async_query_info('standard::size', 'NONE')
            local stream = file:async_read()
            local bytes = stream:async_read_bytes(info:get_size())
            print(bytes.data)
            stream:async_close()
        end
        Gio.Async.start(dump_file)('/etc/passwd')
    end

Note that all reading happens running on background, on_clicked()
handler finished when the operation is still running on background, so
if you have a few gigabytes worth /etc/passwd file, the application
will not freeze while dumping it.

`samples/giostream.lua` provides far more involved sample illustrating
use of asynchronous operations.
