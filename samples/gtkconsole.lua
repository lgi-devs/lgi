#! /usr/bin/env lua

--
-- Lua console using Vte widget.
--

local lgi = require 'lgi'
local Gdk = lgi.Gdk
local Gtk = lgi.Gtk
local Vte = lgi.Vte

-- Create the application.
local app = Gtk.Application { application_id = 'org.lgi.samples.gtkconsole' }

-- Create terminal widget.
local terminal = Vte.Terminal {
   input = '',
}

-- Invoked when something is typed into the terminal.
function terminal:on_commit(text, length)
   if text == '\r' then
      -- Jump to the next line.
      self:feed('\27[E', 3)

      -- Try to execute input line.
      local chunk, msg = loadstring(self.input)
      if not chunk then
	 answer = msg
      else
	 local ok, res = pcall(chunk)
	 answer = tostring(res)
      end
      self:feed(answer, #answer)
      self:feed('\27[E', 3)

      -- Prepare empty input line for the next statement.
      self.input = ''
   else
      -- Simply echo to the terminal and add to the inputline.
      self.input = self.input .. text
      self:feed(text, length)
   end
end

-- Pack terminal into the window with scrollbar.
function app:on_activate()
   local grid = Gtk.Grid {}
   grid.child = terminal
   grid.child = Gtk.Scrollbar {
      orientation = Gtk.Orientation.VERTICAL,
      adjustment = terminal.adjustment,
   }
   local window = Gtk.Window {
      application = self,
      title = 'Lua Terminal',
      default_width = 400,
      default_height = 300,
      has_resize_grip = true,
      child = grid,
   }
   window:show_all()
end

-- Start the application.
app:run { arg[0], ... }
