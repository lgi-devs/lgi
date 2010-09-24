#! /usr/bin/env lua

--
-- Lua console using Vte windget.
--

require 'lgi'
local bit = require 'bit'
local Gdk = require 'lgi.Gdk'
local Gtk = require 'lgi.Gtk'
local Vte = require 'lgi.Vte'

-- Initialize GTK.
Gtk.init()

-- Create top level window with some properties and connect its 'destroy'
-- signal to the event loop termination.
local window = Gtk.Window {
   title = 'window',
   default_width = 400,
   default_height = 300,
   on_destroy = Gtk.main_quit
}

-- Create some more widgets for the window.
local status_bar = Gtk.Statusbar { has_resize_grip = true }
local toolbar = Gtk.Toolbar()
local ctx = status_bar:get_context_id('default')
status_bar:push(ctx, 'This is statusbar message.')

-- When clicking at the toolbar 'quit' button, destroy the main window.
toolbar:insert(Gtk.ToolButton {
		  stock_id = 'gtk-quit',
		  on_clicked = function() window:destroy() end,
	       }, -1)

-- About button in toolbar and its handling.
toolbar:insert(Gtk.ToolButton {
		  stock_id = 'gtk-about',
		  on_clicked = function()
				  local dlg = Gtk.AboutDialog {
				     program_name = 'LGI Lua Terminal',
				     title = 'About...',
				     license = 'MIT'
				  }
				  dlg:run()
				  dlg:hide()
			       end
	       }, -1)

-- Create terminal widget.
local terminal = Vte.Terminal.new {
   on_commit = function(...)
		  print(...)
	       end
}

--terminal:feed(13)

-- Pack everything into the window.
local vbox = Gtk.VBox()
vbox:pack_start(toolbar, false, false, 0)
vbox:pack_start(terminal, true, true, 0)
vbox:pack_end(status_bar, false, false, 0)
window:add(vbox)

-- Show window and start the loop.
window:show_all()
Gtk.main()
