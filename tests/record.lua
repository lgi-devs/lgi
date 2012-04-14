--[[--------------------------------------------------------------------------

  LGI testsuite, record test suite.

  Copyright (c) 2012 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'
local GLib = lgi.GLib
local GObject = lgi.GObject

local check = testsuite.check

-- Basic GObject testing
local record = testsuite.group.new('record')

function record.native()
   local c = GObject.EnumValue()
   local p = c._native
   check(type(p) == 'userdata')
   check(GObject.EnumValue(p) == c)
end
