set -x
sudo make install LUA_VERSION="${LUA_VERSION}"
xvfb-run -a lua -e '
  Gtk = require("lgi").Gtk
  c = Gtk.Grid()
  w = Gtk.Label()
  c:add(w)
  assert(w.parent == c)
  a, b = dofile("lgi/version.lua"), require("lgi.version")
  assert(a == b, string.format("%s == %s", a, b))
'
