#! /usr/bin/env lua

-- Note: demo.ui loaded by this example is copied verbatim from gtk3-demo, and
-- is probably covered by its appropriate license.

-- Import lgi and get Gtk package.
local lgi = require 'lgi'
local Gtk = lgi.Gtk

-- Initialize GTK
Gtk.init(0, nil)

-- Instantiate Gtk.Builder and load resources from ui file.
local builder = Gtk.Builder()
builder:add_from_file('demo.ui')

-- Get top-level window from the builder.
local window = Gtk.Window(builder:get_object('window1'))

-- Connect 'quit' and 'about' signals.
builder:get_object('Quit').on_activate = function(action) window:destroy() end
builder:get_object('About').on_activate =
function(action)
   local about_dlg = builder:get_object('aboutdialog1')
   about_dlg:run()
   about_dlg:hide()
end

-- Connect 'destroy' signal of the main window.
window.on_destroy = function() Gtk.main_quit() end

-- Make sure that main window is visible.
window:show_all()

-- Start the loop.
Gtk.main()
