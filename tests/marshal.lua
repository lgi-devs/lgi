--[[--------------------------------------------------------------------------

  LGI testsuite, specific marshalling tests

  Copyright (c) 2010, 2011 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'

local check = testsuite.check

-- Basic GObject testing
local marshal = testsuite.group.new('marshal')

function marshal.callback_hidedata()
   local GLib = lgi.GLib
   local main_loop = GLib.MainLoop()
   local argc
   GLib.timeout_add(GLib.PRIORITY_DEFAULT, 100,
		    function(...)
		       argc = select('#', ...)
		       main_loop:quit()
		    end)
   main_loop:run()
   check(argc == 0)
end
