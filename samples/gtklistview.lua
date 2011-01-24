#! /usr/bin/env lua

--
-- Listview demo.
--

local lgi = require('lgi')
local Gtk = lgi.require('Gtk', '3.0')
local GObject = lgi.require('GObject', '2.0')

-- Create and populate ListStore model.
local model = Gtk.ListStore.new { GObject.Type.STRING, GObject.Type.BOOLEAN }
for _, row in ipairs { { 'GObject', true }, { 'Gtk+', true },
		       { 'GStreamer', false } } do
   local iter = model:append()
   model.values[iter] = row
end

-- Create rendereres for columns, attach signals to them.
local name_renderer = Gtk.CellRendererText { editable = true }
function name_renderer:on_edited(path, new_text)
   model.values[path][0] = new_text
end

local check_renderer = Gtk.CellRendererToggle()
function check_renderer:on_toggled(path)
   local row = model.values[path]
   row[1] = not row[1]
end

-- Create the application.
local app = Gtk.Application { application_id = 'org.lgi.demo.gtklistview' }
function app:on_activate()
   -- Create treeview and columns.
   local treeview = Gtk.TreeView {
      model = model,
      columns = {
	 Gtk.TreeViewColumn {
	    title = 'Component', clickable = true, resizable = true,
	    expand = true,
	    cell = { name_renderer, { [0] = Gtk.CellRendererText.text } } },
	 Gtk.TreeViewColumn {
	    title = 'LGI Support', align = 0.5,
	    cell = { check_renderer, { [1] = Gtk.CellRendererToggle.active } } }
      }
   }

   -- Create window with treeview in it.
   local window = Gtk.Window {
      title = 'LGI ListView demo', application = self, child = treeview
   }
   window:show_all()
end

return app:run { arg[0], ... }
