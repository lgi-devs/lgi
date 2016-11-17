------------------------------------------------------------------------------
--
--  lgi Gio DBus override module.
--
--  Copyright (c) 2013 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local pairs, ipairs
   = pairs, ipairs

local lgi = require 'lgi'
local core = require 'lgi.core'
local ffi = require 'lgi.ffi'
local ti = ffi.types

local Gio = lgi.Gio
local GLib = lgi.GLib

-- DBus introspection support.

-- All introspection structures are boxed, but they lack proper C-side
-- introspectable constructors.  Work around this limitation by adding
-- custom lgi constructors which create allocated variants of these
-- records.
for _, name in pairs {
   'Annotation', 'Arg' , 'Method', 'Signal', 'Property', 'Interface', 'Node',
} do
   name = 'DBus' .. name .. 'Info'
   local infotype = Gio[name]

   -- Add constructor which properly creates and initializes info.
   function infotype:_new(params)
      -- Create allocated variant of the record, because when
      -- destroyed by g_boxed_free, Info instances automatically sweep
      -- away also all fields which belong to them.
      local struct = core.record.new(self, nil, 1, true)

      -- Initialize ref_count on the instance.
      struct.ref_count = 1

      -- Assign all constructor parameters.
      for name, value in pairs(params or {}) do
	 struct[name] = value
      end
      return struct
   end

   -- Assign proper refsink method.
   infotype._refsink = core.gi.Gio[name].methods.ref
end

-- g_dbus_node_gemerate_xml is busted, has incorrect [out] annotation
-- of its GString argument (which is in fact [in] one).  Fix it by
-- redeclaring it.
Gio.DBusNodeInfo.generate_xml = core.callable.new {
   name = 'Gio.DBusNodeInfo.generate_xml',
   addr = core.gi.Gio.resolve.g_dbus_node_info_generate_xml,
   ret = ti.void, Gio.DBusNodeInfo, ti.uint, GLib.String,
}

-- Add simple 'xml' attribute as facade over a bit hard-to-use
-- generate_xml() method.
Gio.DBusNodeInfo._attribute = {}
function Gio.DBusNodeInfo._attribute:xml()
   local xml = GLib.String('')
   self:generate_xml(0, xml)
   return xml.str
end

-- g_dbus_proxy_get_interface_info() is documented as "Do not unref the returned
-- object", but has transfer=full. Fix this.
if core.gi.Gio.DBusProxy.methods.get_interface_info.return_transfer ~= 'none' then
   Gio.DBusProxy.get_interface_info = core.callable.new {
      addr = core.gi.Gio.resolve.g_dbus_proxy_get_interface_info,
      name = 'DBusProxy.get_interface_info',
      ret = Gio.DBusInterfaceInfo,
      core.gi.Gio.DBusProxy.methods.new_sync.return_type,
   }
end
