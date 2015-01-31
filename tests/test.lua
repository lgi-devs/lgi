--[[--------------------------------------------------------------------------

  LGI core testsuite runner.

  Copyright (c) 2010, 2011 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

-- Available groups.
local groups = {}

-- Testing infrastructure, tests are grouped in testgroup instances.
testsuite = {}
testsuite.group = {}
testsuite.group.__index = testsuite.group

-- Creates new named testgroup.
function testsuite.group.new(name)
   local group = setmetatable({ name = name,
				results = { total = 0, failed = 0 } },
			      testsuite.group)
   groups[#groups + 1] = name
   groups[name] = group
   return group
end

-- Adds new test.
function testsuite.group:__newindex(name, func)
   assert(not self[name], "test already exists in the group")
   rawset(self, name, func)
   rawset(self, #self + 1, name)
end

-- Runs specified test(s), either by numeric id or by regexp mask.
function testsuite.group:run(id)
   local function runfunc(num)
      self.results.total = self.results.total + 1
      if testsuite.verbose then
	 io.write(('%-8s:%3d:%-35s'):format(self.name, num, self[num]))
	 io.flush()
      end
      local ok, msg
      local func = self[self[num]]
      if self.debug then
	 func()
	 ok = true
      else
	 ok, msg = xpcall(func, debug.traceback)
      end
      collectgarbage()
      if not ok then
	 self.results.failed = self.results.failed + 1
	 if not testsuite.verbose then
	    io.write(('%-8s:%3d:%-35s'):format(self.name, num, self[num]))
	 end
	 io.write('FAIL\n             ' .. tostring(msg) .. '\n')
	 return
      end
      if testsuite.verbose then
	 io.write('PASS\n')
      end
   end

   id = id or ''
   self.results.total = 0
   self.results.failed = 0
   if type(id) == 'number' then
      runfunc(id)
   else
      for i = 1, #self do
	 if self[i] ~= 'debug' and self[i]:match(id) then runfunc(i) end
      end
      if (self.results.failed == 0) then
	 io.write(('%-8s: all %d tests passed.\n'):format(
		     self.name, self.results.total))
      else
	 io.write(('%-8s: FAILED %d of %d tests\n'):format(
		     self.name, self.results.failed, self.results.total))
      end
   end
end

-- Fails given test with error, number indicates how many functions on
-- the stack should be skipped when reporting error location.
function testsuite.fail(msg, skip)
   error(msg or 'failure', (skip or 1) + 1)
end
function testsuite.check(cond, msg, skip)
   if not cond then testsuite.fail(msg, (skip or 1) + 1) end
end

-- Helper, checks that given value has requested type and value.
function testsuite.checkv(val, exp, exptype)
   if exptype then
      testsuite.check(type(val) == exptype,
		      string.format("got type `%s', expected `%s'",
				    type(val), exptype), 2)
   end
   testsuite.check(val == exp,
		   string.format("got value `%s', expected `%s'",
				 tostring(val), tostring(exp)), 2)
end

-- Load all known test source files.
local testpath = arg[0]:sub(1, arg[0]:find('[^%/\\]+$') - 1):gsub('[/\\]$', '')
for _, sourcefile in ipairs {
   'gireg.lua',
   'marshal.lua',
   'corocbk.lua',
   'record.lua',
   'gobject.lua',
   'glib.lua',
   'variant.lua',
   'dbus.lua',
   'gtk.lua',
   'cairo.lua',
   'pango.lua',
} do
   dofile(testpath .. '/' .. sourcefile)
end

-- Check for debug mode.
if tests_debug or package.loaded.debugger then
   -- Make logs verbose (do not mute DEBUG level).
   testsuite.verbose = true
   require('lgi').log.DEBUG = 'verbose'
   for _, name in ipairs(groups) do
      groups[name].debug = true
      _G[name] = groups[name]
   end
end

-- Cmdline runner.
local failed = false
if select('#', ...) == 0 then
   -- Run everything.
   for _, group in ipairs(groups) do
      groups[group]:run()
      failed = failed or groups[group].results.failed > 0
   end
else
   -- Run just those which pass the mask.
   for _, mask in ipairs { ... } do
      local group, groupmask = mask:match('^(.-):(.+)$')
      if not group or not groups[group] then
	 io.write(("No test group for mask `%s' found.\n"):format(mask))
	 return 2
      end
      groups[group]:run(groupmask)
      failed = failed or groups[group].results.failed > 0
   end
end

if failed then
   os.exit(1)
end
