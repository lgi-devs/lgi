#! /usr/bin/env lua

-- Lua console implemented using Gtk widgets.

local lgi = require 'lgi'
local Gio = lgi.Gio
local Gtk = lgi.require('Gtk', '3.0')
local Gdk = lgi.require('Gdk', '3.0')
local Pango = lgi.Pango
local GtkSource = lgi.GtkSource

-- Define global actions.
local actions = {
   execute = Gtk.Action {
      name = 'execute', stock_id = Gtk.STOCK_OK, is_important = true,
      label = "_Execute", tooltip = "Execute",
   },

   multiline = Gtk.ToggleAction {
      name = 'multiline', stock_id = Gtk.STOCK_JUSTIFY_LEFT,
      is_important = true,
      label = "_Multiline",
      tooltip = "Switches command entry to multiline mode",
   },

   up = Gtk.Action {
      name = 'up', stock_id = Gtk.STOCK_GO_UP,
      label = "_Previous", tooltip = "Previous command in history",
      sensitive = false,
   },

   down = Gtk.Action {
      name = 'down', stock_id = Gtk.STOCK_GO_DOWN,
      label = "_Next", tooltip = "Next command in history",
      sensitive = false,
   },

   clear = Gtk.Action {
      name = 'clear', stock_id = Gtk.STOCK_CLEAR,
      label = "_Clear", tooltip = "Clear output window",
   },

   about = Gtk.Action {
      name = 'about', stock_id = Gtk.STOCK_ABOUT,
      label = "_About", tooltip = "About",
   },

   quit = Gtk.Action {
      name = 'quit', stock_id = Gtk.STOCK_QUIT,
      label = "_Quit", tooltip = "Quit",
   },
}

-- Output viewer.  Implemented as simple read-only textview, having
-- different tags (text styles) for different types of output.
local output = { tags = {}, tag_table = Gtk.TextTagTable() }

-- Populate tag table with newly created tags.
for tag_name, tag_props in pairs {
   command = { foreground = 'blue' },
   log = { },
   results = { style = Pango.Style.ITALIC },
   error = { weight = Pango.Weight.BOLD, foreground = 'red' },
} do
   output.tags[tag_name] = Gtk.TextTag(tag_props)
   output.tag_table:add(output.tags[tag_name])
end

output.buffer = Gtk.TextBuffer { tag_table = output.tag_table }
output.end_mark = output.buffer:create_mark(
   nil, output.buffer:get_end_iter(), false)

output.view = Gtk.TextView {
   editable = false,
   buffer = output.buffer,
   wrap_mode = Gtk.WrapMode.CHAR,
}

-- Appends given text to output view, with specified tag.
function output.append(text, tag)
   -- Append the text.
   local end_iter = output.buffer:get_end_iter()
   local offset = end_iter:get_offset()
   output.buffer:insert(end_iter, text, -1)
   end_iter = output.buffer:get_end_iter()

   -- Apply proper tag.
   if tag then
      output.buffer:apply_tag(tag, output.buffer:get_iter_at_offset(offset),
			      end_iter)
   end

   -- Scroll so that the end of the buffer is visible, but only in
   -- case that cursor is at the very end of the view.  This avoids
   -- autoscroll when user tries to select something in the output
   -- view.
   local cursor = output.buffer:get_iter_at_mark(output.buffer:get_insert())
   if end_iter:get_offset() == cursor:get_offset() then
      output.view:scroll_mark_onscreen(output.end_mark)
   end
end

-- Command entry widget.  This is Gtk.SourceView set to Lua mode.
local entry = {}
entry.buffer = GtkSource.Buffer {
   language = GtkSource.LanguageManager.get_default():get_language('lua'),
}
entry.view = GtkSource.View {
   buffer = entry.buffer,
   wrap_mode = Gtk.WrapMode.CHAR,
   auto_indent = true,
   tab_width = 4,
}

-- History buffer.
local history = { pos = 1, '' }

-- Moves up/down in the history, updates UI accordingly.
function history:select(pos)
   self[self.pos] = entry.buffer.text
   self.pos = pos
   entry.buffer.text = self[self.pos]
   entry.buffer:place_cursor(entry.buffer:get_end_iter())
   actions.up.sensitive = self.pos > 1
   actions.down.sensitive = self.pos < #self
end

-- History navigation actions.
function actions.up:on_activate()
   history:select(history.pos - 1)
end

function actions.down:on_activate()
   history:select(history.pos + 1)
end

-- EXecute Lua command from entry and log result into output.
function actions.execute:on_activate()
   -- Get contents of the entry.
   local text = entry.buffer.text:gsub('^%s?(=)%s*', 'return ')
   if text == '' then return end

   -- Add command to the output view.
   output.append(text:gsub('\n*$', '\n', 1), output.tags.command)

   -- Try to execute the command.
   local chunk, answer = loadstring(text, '=stdin')
   local tag = output.tags.error
   if not chunk then
      answer = answer:gsub('\n*$', '\n', 1)
   else
      (function(ok, ...)
	  if not ok then
	     answer = tostring(...):gsub('\n*$', '\n', 1)
	  else
	     -- Stringize the results.
	     answer = {}
	     for i = 1, select('#', ...) do
		answer[#answer + 1] = tostring(select(i, ...))
	     end
	     answer = #answer > 0 and table.concat(answer, '\t') .. '\n'
	     tag = output.tags.results
	  end
       end)(pcall(chunk))
   end

   -- Add answer to the output pane.
   if answer then output.append(answer, tag) end

   if tag == output.tags.error then
      -- Try to parse the error and find line to place the cursor
      local _, _, line = answer:find('^stdin:(%d+):')
      if line then
	 entry.buffer:place_cursor(entry.buffer:get_iter_at_line_offset(
				      line - 1, 0))
      end
   else
      -- Store current text as the last item in the history, but avoid
      -- duplicating items.
      history[#history] = (history[#history - 1] ~= text) and text or nil

      -- Add new empty item to the history, point position to it.
      history.pos = #history + 1
      history[history.pos] = ''

      -- Enable/disable history navigation actions.
      actions.up.sensitive = history.pos > 1
      actions.down.sensitive = false

      -- Clear contents of the entry buffer.
      entry.buffer.text = ''
   end
end

-- Intercept assorted keys in order to implement history navigation.
-- Ideally, this should be implemented using Gtk.BindingKey mechanism,
-- but lgi still lacks possibility to derive classes and install new
-- signals, which is needed in order to implement this.
entry.keytable = {
   [Gdk.KEY_Return] = actions.execute,
   [Gdk.KEY_Up] = actions.up,
   [Gdk.KEY_Down] = actions.down,
}

function entry.view:on_key_press_event(event)
   -- Lookup action to be activated for specified key combination.
   local action = entry.keytable[event.keyval]
   local mask = Gdk.ModifierType[event.state]
   local wants_control = actions.multiline.active
      and Gdk.ModifierType.CONTROL_MASK or nil
   if not action or mask.SHIFT_MASK or mask.CONTROL_MASK ~= wants_control then
      return false
   end

   -- Ask textview whether it still wants to consume the key.
   if self:im_context_filter_keypress(event) then return true end

   -- Activate specified action.
   action:activate()

   -- Do not continue distributing the signal to the view.
   return true
end

function actions.about:on_activate()
   local about = Gtk.AboutDialog {
      program_name = 'LGI Lua Console',
      copyright = '(C) Copyright 2011 Pavel Holejšovský',
      authors = { 'Pavel Holejšovský' },
   }
   about.license_type = Gtk.License.MIT_X11
   about:run()
   about:hide()
end

function actions.clear:on_activate()
   output.buffer.text = ''
end

-- On activation, create and wire the whole widget hierarchy.
local app = Gtk.Application { application_id = 'org.lgi.gtkconsole' }
function app:on_activate()
   local grid = Gtk.Grid {
      orientation = Gtk.Orientation.VERTICAL
   }
   local toolbar = Gtk.Toolbar {}
   toolbar:add(Gtk.ToolButton { related_action = actions.clear })
   toolbar:add(Gtk.SeparatorToolItem {})
   local font_button = Gtk.FontButton {}
   font_button.on_notify['font-name'] = function()
      local desc = Pango.FontDescription.from_string(font_button.font_name)
      output.view:override_font(desc)
      entry.view:override_font(desc)
   end
   toolbar:add(Gtk.ToolItem { child = font_button })
   toolbar:add(Gtk.SeparatorToolItem {})
   toolbar:add(Gtk.ToolButton { related_action = actions.about })
   toolbar:add(Gtk.ToolButton { related_action = actions.quit })
   grid:add(toolbar)
   local paned = Gtk.Paned { orientation = Gtk.Orientation.VERTICAL }
   paned:add(Gtk.ScrolledWindow {
		shadow_type = Gtk.ShadowType.IN,
		child = output.view,
	     }, { resize = true })
   output.view.expand = true
   local entry_grid = Gtk.Grid { orientation = Gtk.Orientation.HORIZONTAL }
   entry_grid:add(Gtk.ScrolledWindow {
		     shadow_type = Gtk.ShadowType.IN,
		     child = entry.view,
		  }, { height = 2 })
   entry.view.hexpand = true
   local toolbar = Gtk.Toolbar {
      orientation = Gtk.Orientation.VERTICAL,
      toolbar_style = Gtk.ToolbarStyle.ICONS,
      vexpand = true,
   }
   toolbar:add(Gtk.ToolButton { related_action = actions.execute })
   toolbar:add(Gtk.ToggleToolButton { related_action = actions.multiline })
   toolbar:add(Gtk.SeparatorToolItem {})
   toolbar:add(Gtk.ToolButton { related_action = actions.up })
   toolbar:add(Gtk.ToolButton { related_action = actions.down })
   entry_grid:add(toolbar)
   local indicator = Gtk.Label {
      label = '1:1',
      single_line_mode = true,
      justify = Gtk.Justification.RIGHT,
   }
   entry_grid:add(indicator, { left_attach = 1, top_attach = 1 })
   paned:add(entry_grid, { resize = true })
   grid:add(paned)

   -- Change indicator text when position in the entry changes.
   entry.buffer.on_notify['cursor-position'] = function(buffer)
      local iter = buffer:get_iter_at_mark(buffer:get_insert())
      indicator.label =
	 iter:get_line() + 1 .. ':' .. iter:get_line_offset() + 1
   end

   -- Stick everything into the toplevel window.
   local window = Gtk.Window {
      application = app,
      title = "LGI Lua Console",
      default_width = 800, default_height = 600,
      child = grid,
   }

   -- Initialize font.  Get preferred font from system settings, if
   -- they are installed.  GSettings crash the application if the
   -- schemas are not available :-(
   font_button.font_name = 'Monospace'
   for _, schema in pairs(Gio.Settings.list_schemas()) do
      if schema == 'org.gnome.desktop.interface' then
	 font_button.font_name = Gio.Settings(
	    { schema = schema }):get_string('monospace-font-name')
	 break
      end
   end

   -- Make everything visible
   entry.view.has_focus = true
   window:show_all()

   -- Map quit action to window destroy.
   function actions.quit:on_activate()
      window:destroy()
   end

   -- Inject 'lgi' symbol into global namespace, for convenience.
   -- Also add 'console' table containing important elements of this
   -- console, so that it can be manipulated live.
   _G.lgi = lgi
   _G.console = { window = window, entry = entry, output = output }
end

-- Override global 'print' and 'io.write' handlers, so that output
-- goes to our output window (with special text style).
local old_print = print
local old_write = io.write
function print(...)
   local outs = {}
   for i = 1, select('#', ...) do
      outs[#outs + 1] = tostring(select(i, ...))
   end
   output.append(table.concat(outs, '\t') .. '\n', output.tags.log)
end

function io.write(...)
   for i = 1, select('#', ...) do
      output.append(select(i, ...), output.tags.log)
   end
end

-- Run the whole application
app:run { arg[0], ... }

-- Revert to old printing routines.
print = old_print
io.write = old_write
