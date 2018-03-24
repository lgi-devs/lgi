liblgi = shared_module('corelgilua51',
  sources: [
    'buffer.c',
    'callable.c',
    'core.c',
    'gi.c',
    'marshal.c',
    'object.c',
    'record.c',
  ],
  dependencies: [
    lua_dep,
    gi_dep,
    dependency('libffi'),
    dependency('gmodule-2.0'),
  ],
  name_prefix: '',
  install: true,
  install_dir: join_paths(lua_cpath, 'lgi'),
)

install_data([
  'class.lua',
  'component.lua',
  'core.lua',
  'enum.lua',
  'ffi.lua',
  'init.lua',
  'log.lua',
  'namespace.lua',
  'package.lua',
  'record.lua'
], install_dir: join_paths(lua_path, 'lgi'))

install_subdir('override', install_dir: join_paths(lua_path, 'lgi'))

conf = configuration_data()
conf.set('VERSION', meson.project_version())
configure_file(
  input: 'version.lua.in',
  output: 'version.lua',
  configuration: conf,
  install: true,
  install_dir: join_paths(lua_path, 'lgi'),
)
