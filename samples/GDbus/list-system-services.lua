#! /usr/bin/env lua

local help = [[
Usage: lua list-system-services.lua [--session|--system]
List services on the system bus (default) or the session bus.
]]

--[[
This example is the rewrite of list-system-services.py in Lua.
Copyleft (C) 2012 Ildar Mulyukov
Original copyright follows:
# Copyright (C) 2004-2006 Red Hat Inc. <http://www.redhat.com/>
# Copyright (C) 2005-2007 Collabora Ltd. <http://www.collabora.co.uk/>
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation
# files (the "Software"), to deal in the Software without
# restriction, including without limitation the rights to use, copy,
# modify, merge, publish, distribute, sublicense, and/or sell copies
# of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
]]

local lgi = require 'lgi'
local Gio = lgi.require 'Gio'

-- main(argv):
   local argv = {...}

   local factory = function()
      return Gio.bus_get_sync(Gio.BusType.SYSTEM)
   end

   if #argv > 1 then
        print(help)
        return -1
   else
      if #argv == 1 then
         if argv[1] == '--session' then
            factory = function()
               return Gio.bus_get_sync(Gio.BusType.SESSION)
            end
         else
            if argv[1] ~= '--system' then
               print(help)
               return -1
            end
         end
      end
   end

   -- Get a connection to the system or session bus as appropriate
   -- We're only using blocking calls, so don't actually need a main loop here
   local bus = factory()

   -- This could be done by calling bus.list_names(), but here's
   -- more or less what that means:

   -- Get a reference to the desktop bus' standard object, denoted
   -- by the path /org/freedesktop/DBus.
   -- dbus_object = bus.get_object('org.freedesktop.DBus',
   --                      '/org/freedesktop/DBus')

   -- The object /org/freedesktop/DBus
   -- implements the 'org.freedesktop.DBus' interface
   -- dbus_iface = dbus.Interface(dbus_object, 'org.freedesktop.DBus')

   -- One of the member functions in the org.freedesktop.DBus interface
   -- is ListNames(), which provides a list of all the other services
   -- registered on this bus. Call it, and print the list.
   -- services = dbus_iface.ListNames()
   local var, err = bus:call_sync('org.freedesktop.DBus', 
      '/org/freedesktop/DBus',
      'org.freedesktop.DBus',
      'ListNames',
      nil,
      nil,
      Gio.DBusConnectionFlags.NONE,
      -1)
   -- We know that ListNames returns '(as)'
   local services = var[1].value

   -- services.sort() - is incorrect as GVariant is immutable
   for i = 1, #services do
      print (services[i])
   end
