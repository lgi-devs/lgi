#! /usr/bin/env lua

--[[--------------------------------------------------------------------------

  Sample GTK Application program, simple notepad implementation.

  Copyright (c) 2010 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'
local Gtk = lgi.require('Gtk', '3.0')
local Gio = lgi.Gio

local app = Gtk.Application.new('org.lgi.GtkPad',
				Gio.ApplicationFlags.HANDLES_OPEN)

local function new_editor(file)
   local contents = file and file:load_contents()
   local window = Gtk.Window {
      type = Gtk.WindowType.TOPLEVEL,
      default_width = 400,
      default_height = 300,
      application = app,
      title = file and file:get_parse_name() or '<Untitled>',
      child = Gtk.ScrolledWindow {
	 child = Gtk.TextView {
	    buffer = Gtk.TextBuffer {
	       text = contents and tostring(contents) or ''
	    }
	 }
      }
   }
   window:show_all()
   return window
end

function app:on_activate()
   new_editor()
end

function app:on_open(files)
   for i = 1, #files do new_editor(files[i]) end
end

return app:run {arg[0], ...}
