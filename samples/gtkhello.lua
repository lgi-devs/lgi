#! /usr/bin/env lua

--
-- Sample GTK Hello program
--
-- Based on test from LuiGI code.  Thanks Adrian!
--

require 'lgi'
local Gtk = require 'lgi.Gtk'

-- Initialize GTK.
Gtk.init()

-- Create top level window with some properties and connect its 'destroy'
-- signal to the event loop termination.
local window = Gtk.Window {
   title = 'window',
   default_width = 400,
   default_height = 300,
   has_resize_grip = true,
   on_destroy = Gtk.main_quit
}

-- Create some more widgets for the window.
local status_bar = Gtk.Statusbar()
local toolbar = Gtk.Toolbar()
local ctx = status_bar:get_context_id('default')
status_bar:push(ctx, 'This is statusbar message.')

-- When clicking at the toolbar 'quit' button, destroy the main window.
toolbar:insert(Gtk.ToolButton {
		  stock_id = 'gtk-quit',
		  on_clicked = function() window:destroy() end,
	       }, -1)

-- About button in toolbar and its handling.
local about_button =  Gtk.ToolButton { stock_id = 'gtk-about' }
function about_button:on_clicked()
   local dlg = Gtk.AboutDialog {
      program_name = 'LGI Demo',
      title = 'About...',
      name = 'LGI Hello',
      copyright = '(C) Copyright 2010 Pavel Holejšovský',
      authors = { 'Adrian Perez de Castro', 'Pavel Holejšovský', },
   }
   dlg.license_type = Gtk.License.MIT_X11
   dlg:run()
   dlg:hide()
end
toolbar:insert(about_button, -1)

-- Pack everything into the window.
local vbox = Gtk.VBox()
vbox:pack_start(toolbar, false, false, 0)
vbox:pack_start(Gtk.Label { label = 'Contents' }, true, true, 0)
vbox:pack_end(status_bar, false, false, 0)
window:add(vbox)

-- Show window and start the loop.
window:show_all()
Gtk.main()
