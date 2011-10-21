#! /usr/bin/env python

APPNAME='lgi'
VERSION='0.1'

top = '.'
out = 'build'

def options(opt):
    opt.tool_options('compiler_cc')
    opt.add_option('--enable-debug', action='store_true', dest='debug',
                   default=False, help='enable debugging mode')

def configure(conf):
    import Options
    conf.check_tool('compiler_cc')
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
    conf.check_cfg(package='gobject-introspection-1.0', uselib_store='GI',
                   mandatory=True,
                   args='gobject-introspection-1.0 >= 1.30 --cflags --libs',
                   msg='Checking for gobject-introspection >= 1.30')
    conf.env.append_unique('CCFLAGS', '-Wall')
    conf.env.append_unique('CCFLAGS', Options.options.debug
                           and '-g' or ['-O2', '-DNDEBUG'])

    conf.recurse('tests')

def build(bld):
    bld.recurse('src tests')
