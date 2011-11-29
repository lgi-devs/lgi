--[[--------------------------------------------------------------------------

  LGI testsuite, GObject.Object test suite.

  Copyright (c) 2010, 2011 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'
local GLib = lgi.GLib
local GObject = lgi.GObject

local check = testsuite.check

-- Basic GObject testing
local gobject = testsuite.group.new('gobject')

function gobject.object_new()
   local GObject = lgi.GObject
   local o = GObject.Object()
   o = nil
   collectgarbage()
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
   collectgarbage()
end
