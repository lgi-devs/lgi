#! /usr/bin/env python

APPNAME='lgi'
VERSION='0.3'

top = '.'
out = 'build'

def options(opt):
    opt.tool_options('compiler_cc')
    opt.add_option('--enable-debug', action='store_true', dest='debug',
		   default=False, help='enable debugging mode')
    opt.add_option('--datadir', dest='datadir',
		   help='directory for Lua modules written in Lua')
    opt.add_option('--libdir', dest='libdir',
		   help='directory for Lua modules written in C')

def configure(conf):
    import Options
    import os
    conf.check_tool('compiler_cc')

    # Check for Lua.
    if 'LUA_CFLAGS' not in os.environ:
	lua_found = False
	for lua_pkg in ['lua5.1', 'lua']:
	    if conf.check_cfg(package=lua_pkg, uselib_store='LUA',
			      args='--cflags', mandatory=False) is not None:
		conf.check_cfg(package=lua_pkg, uselib_store='LUA',
			       msg='Checking for Lua package directories',
			       okmsg='ok', mandatory=True,
			       variables=['INSTALL_LMOD', 'INSTALL_CMOD'])
		lua_found = True
		break
	if not lua_found:
	    conf.fatal("Lua pkgconfig package not found.")

    if Options.options.datadir:
	conf.env.LUA_INSTALL_LMOD = Options.options.datadir
    elif not conf.env.LUA_INSTALL_LMOD:
	conf.env.LUA_INSTALL_LMOD = Options.options.prefix + 'share/lua/5.1'
    if Options.options.libdir:
	conf.env.LUA_INSTALL_CMOD = Options.options.libdir
    elif not conf.env.LUA_INSTALL_CMOD:
	conf.env.LUA_INSTALL_CMOD = Options.options.prefix + 'lib/lua/5.1'

    # Check for gobject-introspection package.
    conf.check_cfg(package='gobject-introspection-1.0', uselib_store='GI',
		   mandatory=True,
		   args='gobject-introspection-1.0 >= 0.10.8 --cflags --libs',
		   msg='Checking for gobject-introspection >= 0.10.8')

    # Modify flags according to debug/release build.
    conf.env.append_unique('CCFLAGS', '-Wall')
    conf.env.append_unique('CCFLAGS', Options.options.debug
			   and '-g' or ['-O2', '-DNDEBUG'])
    if Options.options.debug:
	conf.env.tests = True
	conf.recurse('tests')

def build(bld):
    bld.recurse('src')
    if bld.env.tests:
        bld.recurse('tests')
