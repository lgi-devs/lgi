#! /usr/bin/env lua

-- Lua console implemented using Gtk widgets.

local lgi = require 'lgi'
local Gio = lgi.Gio
local Gtk = lgi.require('Gtk', '3.0')
local Gdk = lgi.require('Gdk', '3.0')
local Pango = lgi.Pango
local GtkSource = lgi.GtkSource

-- Creates new console instance.
local function Console()
   -- Define console object actions.
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

   -- Define the widget tree.
   local widget = Gtk.Grid {
      orientation = Gtk.Orientation.VERTICAL,
      Gtk.Toolbar {
	 Gtk.ToolButton { related_action = actions.clear },
	 Gtk.SeparatorToolItem {},
	 Gtk.ToolItem { Gtk.FontButton { id = 'font_button' } },
	 Gtk.SeparatorToolItem {},
	 Gtk.ToolButton { related_action = actions.about },
	 Gtk.ToolButton { related_action = actions.quit },
      },
      Gtk.Paned {
	 orientation = Gtk.Orientation.VERTICAL,
	 {
	    Gtk.ScrolledWindow {
	       shadow_type = Gtk.ShadowType.IN,
	       Gtk.TextView {
		  id = 'output', expand = true,
		  buffer = Gtk.TextBuffer {
		     tag_table = Gtk.TextTagTable {
			Gtk.TextTag { name = 'command', foreground = 'blue' },
			Gtk.TextTag { name = 'log' },
			Gtk.TextTag { name = 'result',
				      style = Pango.Style.ITALIC },
			Gtk.TextTag { name = 'error',
				      weight = Pango.Weight.BOLD,
				      foreground = 'red' },
		     }
		  }
	       }
	    },
	    resize = true
	 },
	 {
	    Gtk.Grid {
	       orientation = Gtk.Orientation.HORIZONTAL,
	       {
		  Gtk.ScrolledWindow {
		     shadow_type = Gtk.ShadowType.IN,
		     GtkSource.View {
			id = 'entry',
			hexpand = true,
			wrap_mode = Gtk.WrapMode.CHAR,
			auto_indent = true,
			tab_width = 4,
			buffer = GtkSource.Buffer {
			   language = GtkSource.LanguageManager.get_default():
			   get_language('lua'),
			},
		     }
		  }, 
		  height = 2
	       },
	       Gtk.Toolbar {
		  orientation = Gtk.Orientation.VERTICAL,
		  toolbar_style = Gtk.ToolbarStyle.ICONS,
		  vexpand = true,
		  Gtk.ToolButton { related_action = actions.execute },
		  Gtk.ToggleToolButton { related_action = actions.multiline },
		  Gtk.SeparatorToolItem {},
		  Gtk.ToolButton { related_action = actions.up },
		  Gtk.ToolButton { related_action = actions.down },
	       },
	       {
		  Gtk.Label {
		     id = 'indicator',
		     label = '1:1',
		     single_line_mode = true,
		     justify = Gtk.Justification.RIGHT,
		  }, 
		  left_attach = 1,
		  top_attach = 1
	       },
	    },
	    resize = true
	 }
      }
   }

   -- Cache important widgets in local variables
   local entry = widget.child.entry
   local output = widget.child.output
   local indicator = widget.child.indicator
   local font_button = widget.child.font_button

   -- When font changes, apply it to both views.
   font_button.on_notify['font-name'] = function(button)
      local desc = Pango.FontDescription.from_string(button.font_name)
      output:override_font(desc)
      entry:override_font(desc)
   end

   -- Initialize font.  Get preferred font from system settings, if
   -- they are installed.  GSettings crash the application if the
   -- schema is not found, so better check first if we can use it.
   font_button.font_name = 'Monospace'
   for _, schema in pairs(Gio.Settings.list_schemas()) do
      if schema == 'org.gnome.desktop.interface' then
	 font_button.font_name = Gio.Settings(
	    { schema = schema }):get_string('monospace-font-name')
	 break
      end
   end

   -- Change indicator text when position in the entry changes.
   entry.buffer.on_notify['cursor-position'] = function(buffer)
      local iter = buffer:get_iter_at_mark(buffer:get_insert())
      indicator.label =
	 iter:get_line() + 1 .. ':' .. iter:get_line_offset() + 1
   end

   local output_end_mark = output.buffer:create_mark(
      nil, output.buffer:get_end_iter(), false)

   -- Appends text to the output window, optionally with specified tag.
   local function append_output(text, tag)
      -- Append the text.
      local end_iter = output.buffer:get_end_iter()
      local offset = end_iter:get_offset()
      output.buffer:insert(end_iter, text, -1)
      end_iter = output.buffer:get_end_iter()

      -- Apply proper tag.
      tag = output.buffer.tag_table.tag[tag]
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
	 output:scroll_mark_onscreen(output_end_mark)
      end
   end

   -- Define history buffer and operations with it.
   local history = { '', position = 1 }
   local function history_select(new_position)
      history[history.position] = entry.buffer.text
      history.position = new_position
      entry.buffer.text = history[history.position]
      entry.buffer:place_cursor(entry.buffer:get_end_iter())
      actions.up.sensitive = history.position > 1
      actions.down.sensitive = history.position < #history
   end

   -- History navigation actions.
   function actions.up:on_activate()
      history_select(history.position - 1)
   end
   function actions.down:on_activate()
      history_select(history.position + 1)
   end

   -- Execute Lua command from entry and log result into output.
   function actions.execute:on_activate()
      -- Get contents of the entry.
      local text = entry.buffer.text:gsub('^%s?(=)%s*', 'return ')
      if text == '' then return end

      -- Add command to the output view.
      append_output(text:gsub('\n*$', '\n', 1), 'command')

      -- Try to execute the command.
      local chunk, answer = (loadstring or load)(text, '=stdin')
      local tag = 'error'
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
		tag = 'result'
	     end
	  end)(pcall(chunk))
      end

      -- Add answer to the output pane.
      if answer then append_output(answer, tag) end

      if tag == 'error' then
	 -- Try to parse the error and find line to place the cursor
	 local line = answer:match('^stdin:(%d+):')
	 if line then
	    entry.buffer:place_cursor(entry.buffer:get_iter_at_line_offset(
					 line - 1, 0))
	 end
      else
	 -- Store current text as the last item in the history, but
	 -- avoid duplicating items.
	 history[#history] = (history[#history - 1] ~= text) and text or nil

	 -- Add new empty item to the history, point position to it.
	 history.position = #history + 1
	 history[history.position] = ''

	 -- Enable/disable history navigation actions.
	 actions.up.sensitive = history.position > 1
	 actions.down.sensitive = false

	 -- Clear contents of the entry buffer.
	 entry.buffer.text = ''
      end
   end

   -- Intercept assorted keys in order to implement history
   -- navigation.  Ideally, this should be implemented using
   -- Gtk.BindingKey mechanism, but lgi still lacks possibility to
   -- derive classes and install new signals, which is needed in order
   -- to implement this.
   local keytable = {
      [Gdk.KEY_Return] = actions.execute,
      [Gdk.KEY_Up] = actions.up,
      [Gdk.KEY_Down] = actions.down,
   }

   function entry:on_key_press_event(event)
      -- Lookup action to be activated for specified key combination.
      local action = keytable[event.keyval]
      local state = event.state
      local without_control = not state.CONTROL_MASK 
      if not action or state.SHIFT_MASK
	 or actions.multiline.active == without_control then
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

   -- Return public object.
   return {
      widget = widget, output = output, entry = entry,
      actions = actions,
      append_output = append_output
   }
end

local old_print = print
local old_write = io.write

-- On activation, create and wire the whole widget hierarchy.
local app = Gtk.Application { application_id = 'org.lgi.gtkconsole' }
function app:on_activate()
   -- Create console.
   local console = Console()

   -- Create application window with console widget in it.
   console.window = Gtk.Window {
      application = app,
      title = "LGI Lua Console",
      default_width = 800, default_height = 600,
      console.widget
   }

   -- Make everything visible
   console.entry.has_focus = true
   console.window:show_all()

   -- Map quit action to window destroy.
   function console.actions.quit:on_activate()
      console.window:destroy()
   end

   -- Inject 'lgi' symbol into global namespace, for convenience.
   -- Also add 'console' table containing important elements of this
   -- console, so that it can be manipulated live.
   _G.lgi = lgi
   _G.console = console

   -- Override global 'print' and 'io.write' handlers, so that output
   -- goes to our output window (with special text style).
   function _G.print(...)
      local outs = {}
      for i = 1, select('#', ...) do
	 outs[#outs + 1] = tostring(select(i, ...))
      end
      console.append_output(table.concat(outs, '\t') .. '\n', 'log')
   end

   function _G.io.write(...)
      for i = 1, select('#', ...) do
	 console.append_output(select(i, ...), 'log')
      end
   end
end

-- Run the whole application
app:run { arg[0], ... }

-- Revert to old printing routines.
print = old_print
io.write = old_write
