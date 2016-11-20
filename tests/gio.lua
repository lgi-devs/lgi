--[[--------------------------------------------------------------------------

  LGI testsuite, GIo test suite.

  Copyright (c) 2016 Uli Schlachter
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local type = type

local lgi = require 'lgi'
local core = require 'lgi.core'

local check = testsuite.check
local checkv = testsuite.checkv

local gio = testsuite.group.new('gio')

function gio.read()
    local GLib, Gio = lgi.GLib, lgi.Gio

    -- Prepare the input to read
    local input
    input = "line"
    input = Gio.MemoryInputStream.new_from_data(input)
    input = Gio.DataInputStream.new(input)

    local line, length

    -- Read line
    line, length = input:read_line()
    checkv(line, "line", "string")
    checkv(length, 4, "number")

    -- Read EOF
    line, length = input:read_line()
    checkv(line, nil, "nil")
    checkv(length, 0, "number")
end

function gio.async_access()
   local Gio = lgi.Gio
   local res

   res = Gio.DBusProxy.async_new
   check(res ~= nil)
   check(type(res) == 'function')

   res = Gio.DBusProxy.async_call
   check(res ~= nil)
   check(type(res) == 'function')

   res = Gio.async_bus_get
   check(res ~= nil)
   check(type(res) == 'function')

   local file = Gio.File.new_for_path('.')
   res = Gio.Async.call(function(target)
			   return target:async_query_info('standard::size',
							  'NONE')
   end)(file)
   check(res ~= nil)

   local b = Gio.Async.call(function()
			       return Gio.async_bus_get('SESSION')
   end)()
   check(Gio.DBusConnection:is_type_of(b))

   local proxy = Gio.Async.call(function(bus)
				   return Gio.DBusProxy.async_new(
				      bus, 'NONE', nil,
				      'org.freedesktop.DBus',
				      '/',
				      'org.freedesktop.DBus')
   end)(b)
   check(Gio.DBusProxy:is_type_of(proxy))
end

