--[[--------------------------------------------------------------------------

  LGI testsuite, coroutine-targetted callbacks

  Copyright (c) 2010, 2011 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local coroutine = require 'coroutine'
local lgi = require 'lgi'
local GLib = lgi.GLib

local check = testsuite.check

-- Basic GObject testing
local corocbk = testsuite.group.new('corocbk')

function corocbk.resume_suspd()
   local GLib = lgi.GLib
   local main_loop = GLib.MainLoop()
   local coro = coroutine.create(
      function()
	 coroutine.yield()
	 coroutine.yield(true)
	 main_loop:quit()
      end)
   coroutine.resume(coro)
   GLib.timeout_add(GLib.PRIORITY_DEFAULT, 100, coro)
   main_loop:run()
end

function corocbk.resume_init()
   local GLib = lgi.GLib
   local main_loop = GLib.MainLoop()
   local coro = coroutine.create(
      function()
	 coroutine.yield(true)
	 main_loop:quit()
      end)
   GLib.timeout_add(GLib.PRIORITY_DEFAULT, 100, coro)
   main_loop:run()
end
