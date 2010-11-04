#! /usr/bin/env lua

--[[--------------------------------------------------------------------------

  Sample GTK Application program, simple notepad implementation.

  Copyright (c) 2010 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

require 'lgi'
local Gtk = require 'lgi.Gtk'
local Gio = require 'lgi.Gio'

local function new_window(app, file)
   local ok, contents = file and file:load_contents()
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
   app.on_activate = function(app)
			new_window(app)
		     end

   app.on_open = function(app, files)
		    for i = 1, #files do new_window(app, files[i]) end
		 end

   return app:run(0, 'prd')
else
   Gtk.init()
   local args, running = { ... }, 0
   for i = 1, #args ~= 0 and #args or 1 do
      new_window(nil, args[i]).on_destroy = function() 
					       count = count - 1
					       if count == 0 then
						  Gtk.main_quit()
					       end
					    end
      count = count + 1
   end
   Gtk.main()
end
