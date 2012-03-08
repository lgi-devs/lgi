#! /usr/bin/env lua

-- Note: demo.ui loaded by this example is copied verbatim from gtk3-demo, and
-- is probably covered by its appropriate license.

-- Import lgi and get Gtk package.
local lgi = require 'lgi'
local Gtk = lgi.Gtk

-- There are two ways to access Gtk.Builder; using standard Gtk API's
-- get_object() and get_objects(), or LGI override shortcuts.  Both
-- can be used, as demonstrated below.
local window
if gtk_builder_use_standard_api then
   -- Instantiate Gtk.Builder and load resources from ui file.
   local builder = Gtk.Builder()
   assert(builder:add_from_file('demo.ui')
       or builder:add_from_file('samples/demo.ui'))

   -- Get top-level window from the builder.
   window = builder:get_object('window1')

   -- Connect 'Quit' and 'About' actions.
   builder:get_object('Quit').on_activate = function(action)
					       window:destroy()
					    end
   builder:get_object('About').on_activate =
   function(action)
      local about_dlg = builder:get_object('aboutdialog1')
      about_dlg:run()
      about_dlg:hide()
   end
else
   -- Instantiate builder and load objects.
   local builder = Gtk.Builder()
   assert(builder:add_from_file('demo.ui')
       or builder:add_from_file('samples/demo.ui'))
   local ui = builder.objects

   -- Get top-level window from the builder.
   window = ui.window1

   -- Connect 'Quit' and 'About' actions.
   function ui.Quit:on_activate() window:destroy() end
   function ui.About:on_activate()
      ui.aboutdialog1:run()
      ui.aboutdialog1:hide()
   end
end

-- Connect 'destroy' signal of the main window, terminates the main loop.
window.on_destroy = Gtk.main_quit

-- Make sure that main window is visible.
window:show_all()

-- Start the loop.
Gtk.main()
