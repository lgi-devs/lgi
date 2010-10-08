#! /usr/bin/env python

APPNAME='lgi'
VERSION='0.1'

top = '.'
out = 'build'

def options(opt):
    opt.tool_options('compiler_cc')
    opt.add_option('--enable-debug', action='store_true', dest='debug',
                   default=False, help='Enable debugging mode.')

def configure(conf):
    import Options
    conf.check_tool('compiler_cc')
    conf.check_cfg(package='lua5.1', uselib_store='LUA',
                   mandatory=True,
                   args='--cflags --libs')
    conf.check_cfg(package='lua5.1', uselib_store='LUA',
                   msg = 'Checking for Lua package directories',
                   okmsg = 'ok',
                   variables=['INSTALL_LMOD', 'INSTALL_CMOD'])
    conf.check_cfg(package='gobject-introspection-1.0', uselib_store='GI',
                   mandatory=True,
                   args='--cflags --libs')
    conf.env.append_unique('CCFLAGS', '-Wall')
    conf.env.append_unique('CCFLAGS', Options.options.debug
                           and '-g' or ['-O2', '-DNDEBUG'])

    conf.recurse('tests')

def build(bld):
    bld.recurse('src tests')

# Install everything into root build dir, so that it is easilly reachable
# during development testing (instead of assorted files scattered under build
# directory).  Implemented by brutal rapture of waf 'install' feature.
from waflib.Build import InstallContext
class DevContext(InstallContext):
    cmd = 'dev'
    def install_files(self, *k, **kw):
        tsk = super(DevContext, self).install_files(*k, **kw)
        tsk.dest = self.bldnode.abspath()
        return tsk
