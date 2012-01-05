#! /usr/bin/env lua

--
-- Implementation of simple browser of lgi repository.  This is mainly
-- treeview usage demonstration.
--

local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.require('Gtk', '3.0')

local function RepoBrowser()
   local self = {}

   -- Create browser model.
   local column = {
      NAME = 1,
      VALUE = 2,
   }
   local model = Gtk.TreeStore.new {
      [column.NAME] = GObject.Type.STRING,
      [column.VALUE] = GObject.Type.STRING,
   }

   -- Merges repository table into the treemodel as child of given
   -- iterator.
   local blacklisted = {
      _parent = true, _implements = true, __index = true,
   }
   local function merge(parent, table)
      for name, value in pairs(table) do
	 local string_value = ''
	 if type(value) == 'string' then
	    string_value = value
	 elseif type(value) == 'number' then
	    string_value = tonumber(value)
	 elseif name == '_parent' then
	    string_value = value._name
	 end
	 local iter = model:append(parent, {
				      [column.NAME] = name,
				      [column.VALUE] = string_value,
				   })
	 if type(value) == 'table' and not blacklisted[name] then
	    merge(iter, value)
	 end
      end
   end

   -- Loads specified repo namespace into the model.
   function self.add(namespace)
      local iter = model:append(nil, {
				   [column.NAME] = namespace._name,
				   [column.VALUE] = namespace._version,
				})
      merge(iter, namespace:_resolve(true))
   end

   -- Create treeview widget with columns.
   local sorted = Gtk.TreeModelSort { model = model }
   sorted:set_sort_func(column.NAME, function(model, a, b)
					a = model[a][column.NAME]
					b = model[b][column.NAME]
					if a == b then return 0
					elseif a < b then return -1
					else return 1 end
				     end)
   sorted:set_sort_column_id(column.NAME, Gtk.SortType.ASCENDING)
   self.view = Gtk.TreeView {
      model = sorted,
      Gtk.TreeViewColumn {
	 title = "Name", resizable = true, sort_column_id = column.NAME,
	 { Gtk.CellRendererText(), expand = true, { text = column.NAME } },
      },
      Gtk.TreeViewColumn {
	 { Gtk.CellRendererText(), { text = column.VALUE } },
      },
   }

   return self
end

-- Create application and its window.
local app = Gtk.Application { application_id = 'org.lgi.repobrowser' }
function app:on_activate()
   local browser = RepoBrowser()
   browser.add(lgi.GObject)

   -- Global window.
   local window = Gtk.Window {
      application = app,
      title = "LGI Repository Browser",
      default_width = 800, default_height = 640,
      Gtk.Grid {
	 row_spacing = 5, column_spacing = 5, margin = 5,
	 { Gtk.Label { label = "Namespace" } },
	 { Gtk.Entry { id = 'name', hexpand = true }, left_attach = 1 },
	 { Gtk.Label { label = "Version" }, left_attach = 2 },
	 { Gtk.Entry { id = 'version', hexpand = true }, left_attach = 3 },
	 { Gtk.Button { id = 'add', label = Gtk.STOCK_ADD, use_stock = true },
	   left_attach = 4 },
	 { Gtk.ScrolledWindow { expand = true, browser.view },
	   left_attach = 0, top_attach = 1, width = 5 },
      }
   }

   function window.child.add:on_clicked()
      local version = window.child.version.text
      if version == '' then version = nil end
      local ok, data = pcall(lgi.require, window.child.name.text, version)
      if ok then
	 browser.add(data)
      else
	 local message = Gtk.MessageDialog {
	    text = "Failed to load requested namespace",
	    secondary_text = data,
	    message_type = Gtk.MessageType.ERROR,
	    buttons = Gtk.ButtonsType.CLOSE,
	    transient_for = window,
	 }
	 message:run()
	 message:destroy()
      end
   end

   window:show_all()
end

app:run { arg[0], ... }
