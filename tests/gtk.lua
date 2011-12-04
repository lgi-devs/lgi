--[[--------------------------------------------------------------------------

  LGI testsuite, Gtk overrides test group.

  Copyright (c) 2010, 2011 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local io = require 'io'
local os = require 'os'
local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.Gtk

local check = testsuite.check
local checkv = testsuite.checkv
local gtk = testsuite.group.new('gtk')

function gtk.widget_style()
   local w = Gtk.ProgressBar()
   local v = GObject.Value(GObject.Type.INT)
   w:style_get_property('xspacing', v)
   checkv(w.style.xspacing, v.value, 'number')
   check(not pcall(function() return w.style.nonexistent end))
end
