gnome = import('gnome')

regress_sources = [
  join_paths(gi_datadir, 'tests', 'regress.c'),
  join_paths(gi_datadir, 'tests', 'regress.h'),
]

libregress = shared_library('regress',
  sources: regress_sources,
  c_args: '-w', # Not our warnings, ignore them
  dependencies: [
    dependency('gio-2.0'),
    dependency('cairo-gobject'),
  ]
)

regress_gir = gnome.generate_gir(libregress,
  sources: regress_sources,
  namespace: 'Regress',
  nsversion: '1.0',
  includes: ['Gio-2.0', 'cairo-1.0'],
)

dbus_run = find_program('dbus-run-session')
test('regress', dbus_run,
  args: [lua_prog.path(), files('test.lua')],
  depends: regress_gir,
  env: [
    'LD_LIBRARY_PATH=' + meson.current_build_dir(),
    'GI_TYPELIB_PATH=' + meson.current_build_dir(),
    # Build dir is added for generated version.lua
    'LUA_PATH=@0@/?.lua;@1@/?.lua'.format(meson.source_root(), meson.build_root()),
    'LUA_CPATH=@0@/?.so;@0@/?.dll'.format(meson.build_root())
  ]
)

test_c = executable('test_c', 'test_c.c', dependencies: lua_dep)
test('multiple states', test_c)
