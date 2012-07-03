package = 'lgi'
version = '0.6.2-1'

description = {
   summary = "Lua bindings to GObject libraries",
   detailed = [[
	 Dynamic Lua binding to any library which is introspectable
	 using gobject-introspection.  Allows using GObject-based libraries
	 directly from Lua.
   ]],
   license = 'MIT/X11',
   homepage = 'http://github.com/pavouk/lgi'
}

supported_platforms = { 'unix' }

source = {
   url = 'git://github.com/pavouk/lgi.git',
   tag = '0.6.2'
}

dependencies = { 'lua >= 5.1' }

build = {
   type = 'make',
   variables = {
      PREFIX = '$(PREFIX)',
      LUA_LIBDIR = '$(LIBDIR)',
      LUA_SHAREDIR = '$(LUADIR)',
      LUA_CFLAGS = '$(CFLAGS) -I$(LUA_INCDIR)',
      LIBFLAG = '$(LIBFLAG)',
   },
   copy_directories = { 'docs', 'samples' }
}
