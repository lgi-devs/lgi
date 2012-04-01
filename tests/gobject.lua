--[[--------------------------------------------------------------------------

  LGI testsuite, GObject.Object test suite.

  Copyright (c) 2010, 2011 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'
local GLib = lgi.GLib
local GObject = lgi.GObject
local Gtk = lgi.Gtk
local core = require 'lgi.core'

local check = testsuite.check

-- Basic GObject testing
local gobject = testsuite.group.new('gobject')

function gobject.env_base()
   local obj = GObject.Object()
   check(type(core.object.env(obj)) == 'table')
   check(core.object.env(obj) == core.object.env(obj))
   check(next(core.object.env(obj)) == nil)
end

function gobject.env_persist()
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
