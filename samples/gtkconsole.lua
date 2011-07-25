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

-- Perform initial terminal setup (enable linewrap and scrolling).
terminal:feed('\27[7h',4)
terminal:feed('\27[r', 3)

-- Invoked when something is typed into the terminal.
function terminal:on_commit(text, length)
   if text == '\r' then
      -- Jump to the next line.
      self:feed('\27[E', 3)

      -- Try to execute input line.
      local chunk, msg = loadstring((self.input:gsub('^%s?(=)%s?', 'return ')))
      if not chunk then
	 answer = msg
      else
	 (function(ok, ...)
	    if not ok then
	       return tostring(...)
	    else
	       answer = {}
	       for i = 1, select('#', ...) do
		  answer[#answer + 1] = tostring(select(i, ...))
	       end
	       answer = #answer > 0 and table.concat(answer, '\t')
	    end
      end)(pcall(chunk))
      end
      if answer then
	 self:feed(answer, #answer)
	 self:feed('\27[E', 3)
      end

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
   terminal.expand = true
   grid.child = Gtk.Scrollbar {
      orientation = Gtk.Orientation.VERTICAL,
      adjustment = terminal.adjustment,
   }
   local window = Gtk.Window {
      application = self,
      title = 'Lua Terminal',
      default_width = 800,
      default_height = 600,
      has_resize_grip = true,
      child = grid,
   }
   function terminal:on_resize_window(width, height)
      print('resize', width, height)
      window:resize(width, height)
   end
   window:show_all()
end

-- Start the application.
app:run { arg[0], ... }
