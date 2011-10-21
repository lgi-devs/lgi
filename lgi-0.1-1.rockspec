package = 'LGI'
version = '0.1-1'

description = {
   summary = "Lua bindings to GObject libraries.",
   detailed = [[
	 Dynamic Lua binding to any library which is introspectable
	 using gobject-introspection.  Allows using Gnome Platform libraries
	 directly from Lua.
   ]],
   license = 'MIT/X11',
   homepage = 'https://gitorious.org/lgi/lgi'
}

source = {
   url = 'git://gitorious.org/lgi/lgi.git'
}

dependencies = {
   "lua ~> 5.1"
}

build = {
   type = 'command',
   build_command = 
      "LUA_CFLAGS=-I$(LUA_INCDIR) python waf configure " ..
      "--prefix=$(PREFIX) --datadir=$(LUADIR) --libdir=$(LIBDIR); " ..
      "python waf build",
   install_command = "python waf install",
}
