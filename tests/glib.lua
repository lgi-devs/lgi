--[[--------------------------------------------------------------------------

  LGI testsuite, GLib test suite.

  Copyright (c) 2013 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'

local check = testsuite.check

-- Basic GLib testing
local glib = testsuite.group.new('glib')

function glib.timer()
   local Timer = lgi.GLib.Timer
   check(Timer.new)
   check(Timer.start)
   check(Timer.stop)
   check(Timer.continue)
   check(Timer.elapsed)
   check(Timer.reset)
   check(not Timer.destroy)

   local timer = Timer()
   check(Timer:is_type_of(timer))
   timer = Timer.new()
   check(Timer:is_type_of(timer))

   local el1, ms1 = timer:elapsed()
   check(type(el1) == 'number')
   check(type(ms1) == 'number')

   for i = 1, 1000000 do end

   local el2, ms2 = timer:elapsed()
   check(el1 < el2)

   timer:stop()
   el2 = timer:elapsed()
   for i = 1, 1000000 do end
   check(timer:elapsed() == el2)
end
