package = 'LGI'
version = '0.1-1'

description = {
   summary = "Lua bindings to GObject libraries",
   detailed = [[
	 Dynamic Lua binding to any library which is introspectable
	 using gobject-introspection.  Allows using GObject-based libraries
	 directly from Lua.
   ]],
   license = 'MIT/X11',
   homepage = 'https://gitorious.org/lgi/lgi'
}

supported_platforms = { "unix" }

source = {
   url = 'git://gitorious.org/lgi/lgi.git',
   tag = '0.1'
}

dependencies = {
   "lua 5.1"
}

build = {
   type = 'command',
   build_command = 
      "LUA_CFLAGS=-I$(LUA_INCDIR) python waf configure " ..
      "--prefix=$(PREFIX) --datadir=$(LUADIR) --libdir=$(LIBDIR); " ..
      "python waf build",
   install_command = "python waf install",
   copy_directories = { 'docs' }
}
