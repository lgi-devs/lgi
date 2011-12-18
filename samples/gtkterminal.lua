#! /usr/bin/env lua

--
-- Lua console using Vte widget.  It uses homegrown poor-man's
-- Lua-only readline implementation (most of the code of this sample,
-- not really related to GLib/Gtk in any way).
--

local lgi = require 'lgi'
local Gtk = lgi.require('Gtk', '3.0')
local Vte = lgi.require('Vte', '2.90')

-- Simple readline implementation with asynchronous interface.
local ReadLine = {}
ReadLine.__index = ReadLine

function ReadLine.new()
   return setmetatable(
      {
	 insert_mode = true,
	 columns = 80,
	 history = {},
      }, ReadLine)
end

function ReadLine:start_line(prompt)
   self.input = ''
   self.pos = 1
   self.prompt = prompt or ''
   self.history_pos = #self.history + 1
   self.display(self.prompt)
end

-- Translates input string position into line/column pair.
local function getpos(rl, pos)
   local full, part = math.modf((pos + #rl.prompt - 1) / rl.columns)
   return full, math.floor(part * rl.columns + 0.5)
end

-- Redisplays currently edited line, moves cursor to newpos, assumes
-- that rl.input is updated with new contents but rl.pos still holds
-- old cursor position.
local function redisplay(rl, newpos, modified)
   if newpos < rl.pos then
      -- Go back with the cursor
      local oldl, oldc = getpos(rl, rl.pos)
      local newl, newc = getpos(rl, newpos)
      if oldl ~= newl then
	 rl.display(('\27[%dA'):format(oldl - newl))
      end
      if oldc ~= newc then
	 rl.display(('\27[%d%s'):format(math.abs(newc - oldc),
					oldc < newc and 'C' or 'D'))
      end
   elseif newpos > rl.pos then
      -- Redraw portion between old and new cursor.
      rl.display(rl.input:sub(rl.pos, newpos - 1))
   end
   rl.pos = newpos
   if modified then
      -- Save cursor, redraw the rest of the string, clear the rest of
      -- the line and screen and restore cursor position back.
      rl.display('\27[s' .. rl.input:sub(newpos, -1) .. '\27[K\27[J\27[u')
   end
end

local bindings = {}
function bindings.default(rl, key)
   if not key:match('%c') then
      rl.input = rl.input:sub(1, rl.pos - 1) .. key
      .. rl.input:sub(rl.pos + (rl.insert_mode and 0 or 1), -1)
      redisplay(rl, rl.pos + 1, rl.insert_mode)
   end
end
function bindings.enter(rl)
   redisplay(rl, #rl.input + 1)
   rl.display('\n')
   rl.commit(rl.input)
end
function bindings.back(rl)
   if rl.pos > 1 then redisplay(rl, rl.pos - 1) end
end
function bindings.forward(rl)
   if rl.pos <= #rl.input then redisplay(rl, rl.pos + 1) end
end
function bindings.home(rl)
   if rl.pos ~= 1 then redisplay(rl, 1) end
end
function bindings.goto_end(rl)
   if rl.pos ~= #rl.input then redisplay(rl, #rl.input + 1) end
end
function bindings.backspace(rl)
   if rl.pos > 1 then
      rl.input = rl.input:sub(1, rl.pos - 2) .. rl.input:sub(rl.pos, -1)
      redisplay(rl, rl.pos - 1, true)
   end
end
function bindings.delete(rl)
   if rl.pos <= #rl.input then
      rl.input = rl.input:sub(1, rl.pos - 1) .. rl.input:sub(rl.pos + 1, -1)
      redisplay(rl, rl.pos, true)
   end
end
function bindings.kill(rl)
   rl.input = rl.input:sub(1, rl.pos - 1)
   redisplay(rl, rl.pos, true)
end
function bindings.clear(rl)
   rl.input = ''
   rl.history_pos = #rl.history + 1
   redisplay(rl, 1, true)
end
local function set_history(rl)
   rl.input = rl.history[rl.history_pos] or ''
   redisplay(rl, 1, true)
   redisplay(rl, #rl.input + 1)
end
function bindings.up(rl)
   if rl.history_pos > 1 then
      rl.history_pos = rl.history_pos - 1
      set_history(rl)
   end
end
function bindings.down(rl)
   if rl.history_pos <= #rl.history then
      rl.history_pos = rl.history_pos + 1
      set_history(rl)
   end
end

-- Real keys are here bound to symbolic names.
local function ctrl(char)
   return string.char(char:byte() - ('a'):byte() + 1)
end

bindings[ctrl'b'] = bindings.back
bindings['\27[D'] = bindings.back
bindings[ctrl'f'] = bindings.forward
bindings['\27[C'] = bindings.forward
bindings[ctrl'a'] = bindings.home
bindings['\27OH'] = bindings.home
bindings[ctrl'e'] = bindings.goto_end
bindings['\27OF'] = bindings.goto_end
bindings[ctrl'h'] = bindings.backspace
bindings[ctrl'd'] = bindings.delete
bindings['\127'] = bindings.delete
bindings[ctrl'k'] = bindings.kill
bindings[ctrl'c'] = bindings.clear
bindings[ctrl'p'] = bindings.up
bindings['\27[A'] = bindings.up
bindings[ctrl'n'] = bindings.down
bindings['\27[B'] = bindings.down
bindings['\r'] = bindings.enter

function ReadLine:receive(key)
   (bindings[key] or bindings.default)(self, key)
end

function ReadLine:add_line(line)
   -- Avoid duplicating lines in history.
   if self.history[#self.history] ~= line then
      self.history[#self.history + 1] = line
   end
end

-- Instantiate terminal widget and couple it with our custom readline.
local terminal = Vte.Terminal {
   delete_binding = Vte.TerminalEraseBinding.ASCII_DELETE,
}
local readline = ReadLine.new()

if Vte.Terminal.on_size_allocate then
   -- 'size_allocate' signal is not present in some older Gtk-3.0.gir files
   -- due to bug in older GI versions.  Make sure that this does not trip us
   -- completely, it only means that readline will not react on the terminal
   -- resize events.
   function terminal:on_size_allocate(rect)
      readline.columns = self:get_column_count()
   end
end

function readline.display(str)
   -- Make sure that \n is always replaced with \r\n.  Also make sure
   -- that after \n, kill-rest-of-line is always issued, so that
   -- random garbage does not stay on the screen.
   str = str:gsub('([^\r]?)\n', '%1\r\n'):gsub('\r\n', '\27[K\r\n')
   terminal:feed(str, #str)
end
function terminal:on_commit(str, length)
   readline.columns = self:get_column_count()
   readline:receive(str)
end
function readline.commit(line)
   -- Try to execute input line.
   line = line:gsub('^%s?(=)%s*', 'return ')
   local chunk, answer = (loadstring or load)(line, '=stdin')
   if chunk then
      (function(ok, ...)
	  if not ok then
	     answer = tostring(...)
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
      readline.display(answer .. '\n')
   end

   -- Store the line into rl history and start reading new line.
   readline:add_line(line)
   readline:start_line(_PROMPT or '> ')
end

-- Create the application.
local app = Gtk.Application { application_id = 'org.lgi.samples.gtkconsole' }

-- Pack terminal into the window with scrollbar.
function app:on_activate()
   local grid = Gtk.Grid { child = terminal }
   grid:add(Gtk.Scrollbar {
	       orientation = Gtk.Orientation.VERTICAL,
	       adjustment = terminal.adjustment,
	 })
   terminal.expand = true
   readline.display [[
This is terminal emulation of standard Lua console.  Enter Lua
commands as in interactive Lua console.  The advantage over standard
console is that in this context, GMainLoop is running, so this
console is ideal for interactive toying with Gtk (and other
mainloop-based) components.  Try following:

Gtk = lgi.Gtk <Enter>
window = Gtk.Window { title = 'Test' } <Enter>
window:show_all() <Enter>
window.title = 'Different' <Enter>

]]
   local window = Gtk.Window {
      application = self,
      title = 'Lua Terminal',
      default_width = 640,
      default_height = 480,
      has_resize_grip = true,
      child = grid,
   }
   window:show_all()
   readline.columns = terminal:get_column_count()
   readline:start_line(_PROMPT or '> ')

   -- For convenience, propagate 'lgi' into the global namespace.
   _G.lgi = lgi
end

-- Start the application.
app:run { arg[0], ... }
