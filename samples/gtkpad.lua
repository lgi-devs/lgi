#! /usr/bin/env lua

--[[--------------------------------------------------------------------------

  Sample GTK Application program, simple notepad implementation.

  Copyright (c) 2010 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'
local Gtk = lgi.Gtk
local Gio = lgi.Gio

local function new_editor(app, file)
   local ok, contents
   if file then ok, contents = file:load_contents() end
   local window = Gtk.Window {
      type = Gtk.WindowType.TOPLEVEL,
      application = app,
      title = file and file:get_parse_name() or '<Untitled>',
      child = Gtk.ScrolledWindow {
	 child = Gtk.TextView {
	    buffer = Gtk.TextBuffer { text = ok and contents or '' }
	 }
      }
   }
   window:show_all()
   return window
end

if false then
   local app = Gtk.Application.new('org.lgi.GtkPad',
				   Gio.ApplicationFlags.HANDLES_OPEN)
   function app:on_activate() new_editor(self) end
   function app:on_open(files)
      for i = 1, #files do new_editor(self, files[i]) end
   end

   return app:run(...)
else
   local args, running = { ... }, 0
   for i = 1, #args ~= 0 and #args or 1 do
      if args[i] then args[i] = Gio.File.new_for_path(args[i]) end
      local window = new_editor(nil, args[i])
      running = running + 1
      function window:on_destroy()
	 running = running - 1
	 if running == 0 then
	    Gtk.main_quit()
	 end
      end
   end
   Gtk.main()
end
