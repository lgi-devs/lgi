------------------------------------------------------------------------------
--
--  GTK demo
--
--  Copyright (c) 2012 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
-- This is a port of gtk-demo, licensed as (LGPL):
--
--  This library is free software; you can redistribute it and/or
--  modify it under the terms of the GNU Library General Public
--  License as published by the Free Software Foundation; either
--  version 2 of the License, or (at your option) any later version.
--
--  This library is distributed in the hope that it will be useful,
--  but WITHOUT ANY WARRANTY; without even the implied warranty of
--  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
--  Library General Public License for more details.
--
--  You should have received a copy of the GNU Library General Public
--  License along with this library; if not, write to the
--  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
--  Boston, MA  02111-1307  USA.
--
------------------------------------------------------------------------------

local loadstring = loadstring or load

local lgi = require 'lgi'
local GObject = lgi.GObject
local Gio = lgi.Gio
local Gtk = lgi.require('Gtk', '3.0')
local Pango = lgi.Pango
local GdkPixbuf = lgi.GdkPixbuf
local GtkSource = lgi.GtkSource

-- Create package for the whole demo.
local GtkDemo = lgi.package 'GtkDemo'

local assert = lgi.assert

local DemoListColumn = {
   TITLE = 1,
   INFO = 2,
   SOURCE = 3,
   STYLE = 4,
   WINDOW = 5,
}

local model = Gtk.TreeStore.new {
   [DemoListColumn.TITLE] = GObject.Type.STRING,
   [DemoListColumn.INFO] = GObject.Type.STRING,
   [DemoListColumn.SOURCE] = GObject.Type.STRING,
   [DemoListColumn.STYLE] = Pango.Style,
   [DemoListColumn.WINDOW] = Gtk.Window,
}

-- Enumerate all demo files in this directory and fill tree model with
-- data about them.
local dir = Gio.File.new_for_commandline_arg(arg[0]):get_parent()
local enum = dir:enumerate_children('standard::name', 'NONE')
while true do
   local info, err = enum:next_file()
   if not info then assert(not err, err) break end
   local name = info:get_name()
   if name:match('^demo-(.+)%.lua$') then
      -- Load source and execute it, to get title and info description.
      local source = tostring(assert(dir:get_child(name):load_contents()))
      local run, title, info = assert(loadstring(source))()

      -- Parse title and create an appropriate tree structure.
      local parent = nil
      for item in title:gmatch('([^/]+)/+') do
	 -- Try to find parent in current level.
	 local new_parent
	 for i, row in model:pairs(parent) do
	    if row[DemoListColumn.TITLE] == item then
	       new_parent = i
	       break
	    end
	 end
	 if not new_parent then
	    new_parent = model:append(parent, { [DemoListColumn.TITLE] = item })
	 end
	 parent = new_parent
      end

      -- Add new item from found or created parent element.
      model:append(parent, {
		      [DemoListColumn.TITLE] = title:match('([^/]+)$'),
		      [DemoListColumn.INFO] = '\n' .. info,
		      [DemoListColumn.SOURCE] = source,
		      [DemoListColumn.STYLE] = 'NORMAL',
		   })
   end
end
enum:close()

-- Use sorted tree model, so that demos are in alphabetical order.
local sorted_model = Gtk.TreeModelSort { model = model }
sorted_model:set_sort_func(
   DemoListColumn.TITLE,
   function(model, a, b)
      a = model[a][DemoListColumn.TITLE]
      b = model[b][DemoListColumn.TITLE]
      if a == b then return 0
      elseif a < b then return -1
      else return 1 end
   end)
sorted_model:set_sort_column_id(DemoListColumn.TITLE, Gtk.SortType.ASCENDING)

-- Create whole widget hierarchy.
local window = Gtk.Window {
   default_width = 600, default_height = 400,
   title = "GTK+ LGI Code Demos",
   Gtk.Paned {
      orientation = 'HORIZONTAL',
      Gtk.ScrolledWindow {
	 hscrollbar_policy = 'NEVER',
	 shadow_type = 'IN',
	 Gtk.TreeView {
	    id = 'tree',
	    model = sorted_model,
	    Gtk.TreeViewColumn {
	       title = "Widget (double click for demo)",
	       { Gtk.CellRendererText {}, { text = DemoListColumn.TITLE,
					    style = DemoListColumn.STYLE } }
	    },
	 },
      },
      Gtk.Notebook {
	 {
	    tab_label = "Info",
	    Gtk.ScrolledWindow {
	       shadow_type = 'IN',
	       Gtk.TextView {
		  id = 'info',
		  buffer = Gtk.TextBuffer {
		     tag_table = Gtk.TextTagTable {
			Gtk.TextTag { name = 'title', font = 'sans 18' }
		     }
		  },
		  editable = false, cursor_visible = false,
		  wrap_mode = 'WORD',
		  pixels_above_lines = 2,
		  pixels_below_lines = 2,
	       }
	    }
	 },
	 {
	    tab_label = "Source",
	    Gtk.ScrolledWindow {
	       shadow_type = 'IN',
	       GtkSource.View {
		  id = 'source',
		  buffer = GtkSource.Buffer {
		     language = GtkSource.LanguageManager.get_default():
		     get_language('lua'),
		  },
		  editable = false, cursor_visible = false,
		  wrap_mode = 'NONE',
	       }
	    },
	 },
      },
   }
}

-- Use monospace font for source view.
window.child.source:override_font(
   Pango.FontDescription.from_string('monospace'))

-- Appends text with specified tag into the buffer.
local function append_text(buffer, text, tag)
   local end_iter = buffer:get_end_iter()
   local offset = end_iter:get_offset()
   buffer:insert(end_iter, text, -1)
   if tag then
      end_iter = buffer:get_end_iter()
      buffer:apply_tag_by_name(tag, buffer:get_iter_at_offset(offset),
			       end_iter)
   end
end

-- Handle changing the treeview selection.
local selection = window.child.tree:get_selection()
selection.mode = 'BROWSE'
function selection:on_changed()
   local model, iter = self:get_selected()
   if not model then return end

   -- Load source view.
   local source = window.child.source.buffer
   source.text = model[iter][DemoListColumn.SOURCE] or ''

   -- Load info view.
   local info = window.child.info.buffer
   info:delete(info:get_bounds())
   append_text(info, model[iter][DemoListColumn.TITLE], 'title')
   append_text(info, model[iter][DemoListColumn.INFO] or '')
end

-- Handle activation of the treeview - running the demo.
function window.child.tree:on_row_activated(path)
   -- Convert both model and path to underlying model, not sorted one.
   local model = self.model.model
   local orig_path = path
   path = self.model:convert_path_to_child_path(path)

   -- Get source as string from the model.
   local row = model[path]
   local source = row[DemoListColumn.SOURCE]
   local child_window = row[DemoListColumn.WINDOW]
   if child_window then
      -- Window already existed, destroy it.
      child_window:destroy()
   elseif source then
      -- Run it, get run function from it and run it again with toplevel
      -- window and data directory to use.
      local func = (loadstring or load)(source)()
      child_window = func(window, dir)
      if child_window then
	 -- Signalize that the window is active.
	 row[DemoListColumn.WINDOW] = child_window
	 row[DemoListColumn.STYLE] = 'ITALIC'

	 -- Register destroy signal which will remove the window from
	 -- the store and make style back to normal.
	 function child_window:on_destroy()
	    local row = model[path]
	    row[DemoListColumn.STYLE] = 'NORMAL'
	    row[DemoListColumn.WINDOW] = nil
	 end
      end
   end
end

local app = Gtk.Application { application_id = 'org.lgi.gtk-demo' }
function app:on_activate()
   -- Setup default application icon.
   local pixbuf, err = GdkPixbuf.Pixbuf.new_from_file(
      dir:get_child('gtk-logo-rgb.gif'):get_path())
   if pixbuf then
      -- Add transparency instead of white background.
      local alpha = pixbuf:add_alpha(true, 0xff, 0xff, 0xff)
      Gtk.Window.set_default_icon(alpha)
   else
      -- Report the error.
      local dialog = Gtk.MessageDialog {
	 message_type = 'ERROR', buttons = 'CLOSE',
	 text = ("Failed to read icon file: %s"):format(err),
	 on_response = Gtk.Widget.destroy
      }
      dialog:show_all()
   end

   -- Assign the window as the application one and display it.
   window.application = self
   window:show_all()
end

-- Run the whole application with all commandline arguments.
app:run { arg[0], ... }
