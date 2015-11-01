--[[--------------------------------------------------------------------------

  LGI testsuite, GObject.Object test suite.

  Copyright (c) 2010, 2011 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'
local core = require 'lgi.core'

local check = testsuite.check
local checkv = testsuite.checkv

-- Basic GObject testing
local gobject = testsuite.group.new('gobject')

function gobject.env_base()
   local GObject = lgi.GObject
   local obj = GObject.Object()
   check(type(core.object.env(obj)) == 'table')
   check(core.object.env(obj) == core.object.env(obj))
   check(next(core.object.env(obj)) == nil)
end

function gobject.env_persist()
   local Gtk = lgi.Gtk
   local window = Gtk.Window()
   local label = Gtk.Label()
   local env = core.object.env(label)
   window:_method_add(label)
   label = nil
   collectgarbage()
   label = window:get_child()
   check(env == core.object.env(label))
end

function gobject.object_new()
   local GObject = lgi.GObject
   local o = GObject.Object()
   o = nil
   collectgarbage()
end

function gobject.initunk_new()
   local GObject = lgi.GObject
   local o = GObject.InitiallyUnowned()

   -- Simulate sink by external container
   o:ref_sink()
   o:unref()

   o = nil
   collectgarbage()
end

function gobject.native()
   local GObject = lgi.GObject
   local o = GObject.Object()
   local p = o._native
   check(type(p) == 'userdata')
   check(GObject.Object(p) == o)
end

function gobject.gtype_create()
   local GObject = lgi.GObject
   local Gio = lgi.Gio
   local m = GObject.Object.new(Gio.ThemedIcon, { name = 'icon' })
   check(Gio.ThemedIcon:is_type_of(m))
end

function gobject.subclass_derive1()
   local GObject = lgi.GObject
   local Derived = GObject.Object:derive('LgiTestDerived1')
   local der = Derived()
   check(Derived:is_type_of(der))
   check(not Derived:is_type_of(GObject.Object()))
end

function gobject.subclass_derive2()
   local GObject = lgi.GObject
   local Derived = GObject.Object:derive('LgiTestDerived2')
   local Subderived = Derived:derive('LgiTestSubDerived2')
   local der = Derived()
   check(Derived:is_type_of(der))
   check(not Subderived:is_type_of(der))
   local sub = Subderived()
   check(Subderived:is_type_of(sub))
   check(Derived:is_type_of(sub))
   check(GObject.Object:is_type_of(sub))
end

function gobject.subclass_override1()
   local GObject = lgi.GObject
   local Derived = GObject.Object:derive('LgiTestOverride1')
   local state = 0
   local obj
   function Derived:do_constructed()
      obj = self
      state = state + 1
   end
   function Derived:do_dispose()
      state = state + 2
   end
   check(state == 0)
   local der = Derived()
   check(der == obj)
   check(state == 1)
   der = nil
   obj = nil
   collectgarbage()
   check(state == 3)
end

function gobject.subclass_override2()
   local GObject = lgi.GObject
   local state = 0
   local Derived = GObject.Object:derive('LgiTestOverride2')
   function Derived:do_constructed()
      self.priv.id = 1
      state = state + 1
   end
   function Derived:do_dispose()
      state = state + 2
      GObject.Object.do_dispose(self)
   end
   function Derived:custom_method()
      state = state + 8
      self.priv.id = self.priv.id + 4
   end
   local Subderived = Derived:derive('LgiTestOverrideSub2')
   function Subderived:do_constructed()
      Derived.do_constructed(self)
      self.priv.id = self.priv.id + 2
      state = state + 4
   end
   check(state == 0)
   local sub = Subderived()
   check(state == 5)
   check(sub.priv.id == 3)
   sub:custom_method()
   check(state == 13)
   check(sub.priv.id == 7)
   sub = nil
   collectgarbage()
   check(state == 15)
end

function gobject.subclass_derive3()
   local GObject = lgi.GObject
   local history = {}
   local Derived = GObject.InitiallyUnowned:derive('LgiTestDerived3')
   function Derived:_class_init()
      history[#history + 1] = 'class_init'
      check(self == Derived)
      collectgarbage()
   end
   function Derived:_init()
      history[#history + 1] = 'init'
      collectgarbage()
   end
   function Derived:do_constructed()
      history[#history + 1] = 'constructed'
      Derived._parent.do_constructed(self)
      collectgarbage()
   end
--   function Derived:do_dispose()
--      history[#history + 1] = 'dispose'
--      Derived._parent.do_dispose(self)
--   end
   local obj = Derived()
   check(history[1] == 'class_init')
   check(history[2] == 'init')
   check(history[3] == 'constructed')
   obj = nil
   collectgarbage()
--   check(history[4] == 'dispose')
end

function gobject.iface_virtual()
   local Gio = lgi.Gio
   local file = Gio.File.new_for_path('hey')
   check(file:is_native() == file:do_is_native())
   check(file:get_basename() == file:do_get_basename())
end

function gobject.iface_impl()
   local GObject = lgi.GObject
   local Gio = lgi.Gio
   local FakeFile = GObject.Object:derive('LgiTestFakeFile1', { Gio.File })
   function FakeFile:do_get_basename()
      return self.priv.basename
   end
   function FakeFile:set_basename(basename)
      self.priv.basename = basename
   end
   local fakefile = FakeFile()
   fakefile:set_basename('fakename')
   check(fakefile:get_basename() == 'fakename')
end

function gobject.treemodel_impl()
   local GObject = lgi.GObject
   local Gtk = lgi.Gtk
   local Model = GObject.Object:derive('LgiTestModel1', { Gtk.TreeModel })
   function Model:do_get_n_columns()
      return 2
   end

   function Model:do_get_column_type(index)
      return index == 0 and GObject.Type.STRING or GObject.Type.INT
   end

   function Model:do_get_iter(path)
      local iter = Gtk.TreeIter()
      iter.user_data = path._native
      return iter
   end

   function Model:do_get_value(iter, column)
      return GObject.Value(self:get_column_type(column), 1)
   end

   local model = Model()
   check(model:get_n_columns() == 2)
   check(model:get_column_type(0) == GObject.Type.STRING)
   check(model:get_column_type(1) == GObject.Type.INT)

   local p = Gtk.TreePath.new_from_string('0')
   local i = model:get_iter(p)
   check(i.user_data == p._native)

   check(model[Gtk.TreeIter()][1] == '1')
   check(model[Gtk.TreeIter()][2] == 1)
end

-- Lua 5.3 and 5.1 seems to have some problem with aggressive GC settings used
-- here and completing this test takes literally ages, so skip it.  Enable only if explicitely asked for.
if rawget(_G, '_LGI_ENABLE_ALL_TESTS') then
function gobject.ctor_gc()
   local Gtk = lgi.Gtk

   local oldpause = collectgarbage('setpause', 10)
   local oldstepmul = collectgarbage('setstepmul', 10000)

   for i = 1, 1000 do
      local window = Gtk.Window {
	 width = 800,
	 height = 600,
	 title = "Test",
      }
   end

   collectgarbage('setpause', oldpause)
   collectgarbage('setstepmul', oldstepmul)
end
end

function gobject.subclass_prop1()
   local GObject = lgi.GObject
   local Derived = GObject.Object:derive('LgiTestDerivedProp1')
   Derived._property.string = GObject.ParamSpecString(
      'string', 'Nick string', 'Blurb string', 'string-default',
      { 'READABLE', 'WRITABLE', 'CONSTRUCT' }
   )
   local der = Derived()
   checkv(der.string, 'string-default', 'string')
   der.string = 'assign'
   checkv(der.string, 'assign', 'string')
   checkv(der.priv.string, 'assign', 'string')
end

function gobject.subclass_prop_inherit()
   local GObject = lgi.GObject
   local Gio = lgi.Gio
   local FakeMonitor = GObject.Object:derive(
      'LgiTestFakeMonitor1', { Gio.Initable, Gio.NetworkMonitor })
   FakeMonitor._property.network_available =
      GObject.ParamSpecBoolean('network-available',
			       'LgiTestFakeMonitor1NetworkAvailable',
			       'Whether the network is available.',
			       false, { GObject.ParamFlags.READABLE })
   FakeMonitor._property.connectivity =
      GObject.ParamSpecEnum('connectivity',
			    'LgiTestFakeMonitor1Connectivity',
			    'Type of connectivity.',
			    Gio.NetworkConnectivity,
			    Gio.NetworkConnectivity.LOCAL,
			    { GObject.ParamFlags.READABLE })
   function FakeMonitor:do_init(cancellable)
      self.priv.inited = true
      return true
   end
   local fakemonitor = FakeMonitor()

   -- Gio.Initable is invoked automatically by lgi after object
   -- construction.
   check(fakemonitor.priv.inited)

   -- If the object is not constructed yet, it defaults to false
   check(fakemonitor:get_network_available() == false)
   check(fakemonitor.network_available == false)

   fakemonitor.priv.network_available = true
   check(fakemonitor.network_available == true)
   check(fakemonitor:get_network_available() == true)

   fakemonitor.priv.network_available = false
   check(fakemonitor.network_available == false)
   check(fakemonitor:get_network_available() == false)
end

function gobject.subclass_prop_getset()
   local GObject = lgi.GObject
   local Derived = GObject.Object:derive('LgiTestDerivedPropGetSet')
   Derived._property.str = GObject.ParamSpecString(
      'str', 'Nick string', 'Blurb string', 'string-default',
      { 'READABLE', 'WRITABLE', 'CONSTRUCT' }
   )
   local propval
   function Derived._property_set:str(new_value)
      propval = new_value
   end
   function Derived._property_get:str()
      return propval
   end

   local der = Derived()
   checkv(der.str, 'string-default', 'string')
   der.str = 'assign'
   checkv(der.str, 'assign', 'string')
   checkv(propval, 'assign', 'string')
end

function gobject.signal_query()
   local GObject = lgi.GObject
   local id = GObject.signal_lookup('notify', GObject.Object)
   check(id ~= 0)
   local query = GObject.signal_query(id)
   check(query.signal_id == id)
   check(query.signal_name == 'notify')
   check(query.n_params == 1)
   check(#query.param_types == 1)
   check(query.param_types[1] == GObject.Type.name(GObject.Type.PARAM))
end
