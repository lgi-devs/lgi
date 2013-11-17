--[[--------------------------------------------------------------------------

  lgi testsuite, Gio DBus test suite.

  Copyright (c) 2013 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'

local check = testsuite.check

-- Basic GLib testing
local dbus = testsuite.group.new('dbus')

function dbus.info_basic()
   local Gio = lgi.Gio
   local node = Gio.DBusNodeInfo {
      path = '/some/path',
      interfaces = {
	 Gio.DBusInterfaceInfo {
	    name = 'SomeInterface',
	    methods = {
	       Gio.DBusMethodInfo {
		  name = 'SomeMethod',
		  in_args = {
		     Gio.DBusArgInfo {
			name = 'args',
			signature = 's',
		     },
		     Gio.DBusArgInfo {
			name = 'argi',
			signature = 'i',
		     },
		  },
	       },
	    },
	    properties = {
	       Gio.DBusPropertyInfo {
		  name = 'someProperty',
		  signature = 's',
		  flags = { 'READABLE', 'WRITABLE' },
	       },
	    },
	 },
      },
   }

   check(node.path == '/some/path')
   check(node.ref_count == 1)
   check(node.interfaces[1].name == 'SomeInterface')
   check(node.interfaces[1].methods[1].name == 'SomeMethod')
   check(#node.interfaces[1].methods[1].in_args == 2)
   check(node.interfaces[1].methods[1].in_args[1].name == 'args')
   check(node.interfaces[1].methods[1].in_args[1].signature == 's')
   check(node.interfaces[1].methods[1].in_args[2].name == 'argi')
   check(node.interfaces[1].methods[1].in_args[2].signature == 'i')
   check(#node.interfaces[1].methods[1].out_args == 0)
   check(#node.interfaces[1].properties == 1)
   check(node.interfaces[1].properties[1].name == 'someProperty')
   check(node.interfaces[1].properties[1].signature == 's')
   check(node.interfaces[1].properties[1].flags.READABLE)
   check(node.interfaces[1].properties[1].flags.WRITABLE)
end

function dbus.info_xml()
   local GLib = lgi.GLib
   local Gio = lgi.Gio
   local node = Gio.DBusNodeInfo {
      path = '/some/path',
      interfaces = {
	 Gio.DBusInterfaceInfo {
	    name = 'SomeInterface',
	    methods = {
	       Gio.DBusMethodInfo {
		  name = 'SomeMethod',
		  in_args = {
		     Gio.DBusArgInfo {
			name = 'args',
			signature = 's',
		     },
		     Gio.DBusArgInfo {
			name = 'argi',
			signature = 'i',
		     },
		  },
	       },
	    },
	    properties = {
	       Gio.DBusPropertyInfo {
		  name = 'someProperty',
		  signature = 's',
		  flags = { 'READABLE', 'WRITABLE' },
	       },
	    },
	 },
      },
   }

   local xml_builder = GLib.String('')
   node:generate_xml(0, xml_builder)
   local xml = node.xml
   check(xml_builder.str == xml)

   local node = Gio.DBusNodeInfo.new_for_xml(xml)
   check(node)
   check(node.path == '/some/path')
   check(node.ref_count == 1)
   check(node.interfaces[1].name == 'SomeInterface')
   check(node.interfaces[1].methods[1].name == 'SomeMethod')
   check(#node.interfaces[1].methods[1].in_args == 2)
   check(node.interfaces[1].methods[1].in_args[1].name == 'args')
   check(node.interfaces[1].methods[1].in_args[1].signature == 's')
   check(node.interfaces[1].methods[1].in_args[2].name == 'argi')
   check(node.interfaces[1].methods[1].in_args[2].signature == 'i')
   check(#node.interfaces[1].methods[1].out_args == 0)
   check(#node.interfaces[1].properties == 1)
   check(node.interfaces[1].properties[1].name == 'someProperty')
   check(node.interfaces[1].properties[1].signature == 's')
   check(node.interfaces[1].properties[1].flags.READABLE)
   check(node.interfaces[1].properties[1].flags.WRITABLE)
end
