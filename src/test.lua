--[[-- Assorted tests. --]]--

require 'lgi'
local GLib = require 'lgi.GLib'
local Gio = require 'lgi.Gio'

local tests = {}

-- Helper, checks that given value has requested type and value.
local function checkv(val, exp, exptype)
   assert(type(val) == exptype, string.format("got type `%s', expected `%s'",
					      type(val), exptype))
   assert(val == exp, string.format("got value `%s', expected `%s'",
				    tostring(val), tostring(exp)))
end

-- gobject-introspection 'Regress' based tests.
function tests.t01_gireg_01_boolean()
   local R = lgi.Regress
   checkv(R.test_boolean(true), true, 'boolean')
   checkv(R.test_boolean(false), false, 'boolean')
end

function tests.t01_gireg_02_int8()
   local R = lgi.Regress
   checkv(R.test_int8(0), 0, 'number')
   checkv(R.test_int8(1), 1, 'number')
   checkv(R.test_int8(-1), -1, 'number')
   checkv(R.test_int8(1.1), 1, 'number')
   checkv(R.test_int8(-1.1), -1, 'number')
   checkv(R.test_int8(128), -128, 'number')
   checkv(R.test_int8(255), -1, 'number')
   checkv(R.test_int8(256), 0, 'number')
end

function tests.t01_gireg_03_uint8()
   local R = lgi.Regress
   checkv(R.test_uint8(0), 0, 'number')
   checkv(R.test_uint8(1), 1, 'number')
   checkv(R.test_uint8(-1), 255, 'number')
   checkv(R.test_uint8(1.1), 1, 'number')
   checkv(R.test_uint8(-1.1), 255, 'number')
   checkv(R.test_uint8(128), 128, 'number')
   checkv(R.test_uint8(255), 255, 'number')
   checkv(R.test_uint8(256), 0, 'number')
end

function tests.t01_gireg_04_int16()
   local R = lgi.Regress
   checkv(R.test_int16(0), 0, 'number')
   checkv(R.test_int16(1), 1, 'number')
   checkv(R.test_int16(-1), -1, 'number')
   checkv(R.test_int16(1.1), 1, 'number')
   checkv(R.test_int16(-1.1), -1, 'number')
   checkv(R.test_int16(32768), -32768, 'number')
   checkv(R.test_int16(65535), -1, 'number')
   checkv(R.test_int16(65536), 0, 'number')
end

function tests.t01_gireg_05_uint16()
   local R = lgi.Regress
   checkv(R.test_uint16(0), 0, 'number')
   checkv(R.test_uint16(1), 1, 'number')
   checkv(R.test_uint16(-1), 65535, 'number')
   checkv(R.test_uint16(1.1), 1, 'number')
   checkv(R.test_uint16(-1.1), 65535, 'number')
   checkv(R.test_uint16(32768), 32768, 'number')
   checkv(R.test_uint16(65535), 65535, 'number')
   checkv(R.test_uint16(65536), 0, 'number')
end

function tests.t01_gireg_06_int32()
   local R = lgi.Regress
   checkv(R.test_int32(0), 0, 'number')
   checkv(R.test_int32(1), 1, 'number')
   checkv(R.test_int32(-1), -1, 'number')
   checkv(R.test_int32(1.1), 1, 'number')
   checkv(R.test_int32(-1.1), -1, 'number')
   checkv(R.test_int32(0x80000000), -0x80000000, 'number')
   checkv(R.test_int32(0xffffffff), -1, 'number')
   checkv(R.test_int32(0x100000000), 0, 'number')
end

function tests.t01_gireg_07_uint32()
   local R = lgi.Regress
   checkv(R.test_uint32(0), 0, 'number')
   checkv(R.test_uint32(1), 1, 'number')
   checkv(R.test_uint32(-1), 0xffffffff, 'number')
   checkv(R.test_uint32(1.1), 1, 'number')
   checkv(R.test_uint32(-1.1), 0xffffffff, 'number')
   checkv(R.test_uint32(0x80000000), 0x80000000, 'number')
   checkv(R.test_uint32(0xffffffff), 0xffffffff, 'number')
   checkv(R.test_uint32(0x100000000), 0, 'number')
end

function tests.t01_gireg_08_int64()
   local R = lgi.Regress
   checkv(R.test_int64(0), 0, 'number')
   checkv(R.test_int64(1), 1, 'number')
   checkv(R.test_int64(-1), -1, 'number')
   checkv(R.test_int64(1.1), 1, 'number')
   checkv(R.test_int64(-1.1), -1, 'number')

   checkv(R.test_int64(0x80000000), 0x80000000, 'number')
   checkv(R.test_int64(0xffffffff), 0xffffffff, 'number')
   checkv(R.test_int64(0x100000000), 0x100000000, 'number')

-- Following tests fail because Lua's internal number representation
-- is always 'double', and conversion between double and int64 big
-- constants is always lossy.  Not sure if it can be solved somehow.

-- checkv(R.test_int64(0x8000000000000000), -0x8000000000000000, 'number')
-- checkv(R.test_int64(0xffffffffffffffff), -1, 'number')
-- checkv(R.test_int64(0x10000000000000000), 0, 'number')
end

function tests.t01_gireg_09_uint64()
   local R = lgi.Regress
   checkv(R.test_uint64(0), 0, 'number')
   checkv(R.test_uint64(1), 1, 'number')
   checkv(R.test_uint64(-1), 0xffffffffffffffff, 'number')
   checkv(R.test_uint64(1.1), 1, 'number')
   checkv(R.test_uint64(-1.1), 0xffffffffffffffff, 'number')

   checkv(R.test_uint64(0x80000000), 0x80000000, 'number')
   checkv(R.test_uint64(0xffffffff), 0xffffffff, 'number')
   checkv(R.test_uint64(0x100000000), 0x100000000, 'number')
   checkv(R.test_uint64(0x8000000000000000), 0x8000000000000000, 'number')
   checkv(R.test_uint64(0x10000000000000000), 0, 'number')

-- See comment above about lossy conversions.
-- checkv(R.test_uint64(0xffffffffffffffff), 0xffffffffffffffff, 'number')
end

function tests.t01_gireg_10_short()
   local R = lgi.Regress
   checkv(R.test_short(0), 0, 'number')
   checkv(R.test_short(1), 1, 'number')
   checkv(R.test_short(-1), -1, 'number')
   checkv(R.test_short(1.1), 1, 'number')
   checkv(R.test_short(-1.1), -1, 'number')
end

function tests.t01_gireg_11_ushort()
   local R = lgi.Regress
   checkv(R.test_ushort(0), 0, 'number')
   checkv(R.test_ushort(1), 1, 'number')
   checkv(R.test_ushort(1.1), 1, 'number')
   checkv(R.test_ushort(32768), 32768, 'number')
   checkv(R.test_ushort(65535), 65535, 'number')
end

function tests.t01_gireg_12_int()
   local R = lgi.Regress
   checkv(R.test_int(0), 0, 'number')
   checkv(R.test_int(1), 1, 'number')
   checkv(R.test_int(-1), -1, 'number')
   checkv(R.test_int(1.1), 1, 'number')
   checkv(R.test_int(-1.1), -1, 'number')
end

function tests.t01_gireg_13_uint()
   local R = lgi.Regress
   checkv(R.test_uint(0), 0, 'number')
   checkv(R.test_uint(1), 1, 'number')
   checkv(R.test_uint(1.1), 1, 'number')
   checkv(R.test_uint(0x80000000), 0x80000000, 'number')
   checkv(R.test_uint(0xffffffff), 0xffffffff, 'number')
end

function tests.t01_gireg_14_ssize()
   local R = lgi.Regress
   checkv(R.test_ssize(0), 0, 'number')
   checkv(R.test_ssize(1), 1, 'number')
   checkv(R.test_ssize(-1), -1, 'number')
   checkv(R.test_ssize(1.1), 1, 'number')
   checkv(R.test_ssize(-1.1), -1, 'number')
end

function tests.t01_gireg_15_size()
   local R = lgi.Regress
   checkv(R.test_size(0), 0, 'number')
   checkv(R.test_size(1), 1, 'number')
   checkv(R.test_size(1.1), 1, 'number')
   checkv(R.test_size(0x80000000), 0x80000000, 'number')
   checkv(R.test_size(0xffffffff), 0xffffffff, 'number')
end

-- Helper, checks that given value has requested type and value.
local function checkvf(val, exp, tolerance)
   assert(type(val) == 'number', string.format(
	     "got type `%s', expected `number'", type(val)))
   assert(math.abs(val - exp) <= tolerance, 
	  string.format("got value `%s', expected `%s'",
			tostring(val), tostring(exp)))
end

function tests.t01_gireg_16_float()
   local R = lgi.Regress
   local t = 0.000001
   checkvf(R.test_float(0), 0, t)
   checkvf(R.test_float(1), 1, t)
   checkvf(R.test_float(1.1), 1.1, t)
   checkvf(R.test_float(-1), -1, t)
   checkvf(R.test_float(-1.1), -1.1, t)
   checkvf(R.test_float(0x8000), 0x8000, t)
   checkvf(R.test_float(0xffff), 0xffff, t)
   checkvf(R.test_float(-0x8000), -0x8000, t)
   checkvf(R.test_float(-0xffff), -0xffff, t)
end

function tests.t01_gireg_17_double()
   local R = lgi.Regress
   checkv(R.test_double(0), 0, 'number')
   checkv(R.test_double(1), 1, 'number')
   checkv(R.test_double(1.1), 1.1, 'number')
   checkv(R.test_double(-1), -1, 'number')
   checkv(R.test_double(-1.1), -1.1, 'number')
   checkv(R.test_double(0x80000000), 0x80000000, 'number')
   checkv(R.test_double(0xffffffff), 0xffffffff, 'number')
   checkv(R.test_double(-0x80000000), -0x80000000, 'number')
   checkv(R.test_double(-0xffffffff), -0xffffffff, 'number')
end

function tests.t01_gireg_18_timet()
   local R = lgi.Regress
   checkv(R.test_timet(0), 0, 'number')
   checkv(R.test_timet(1), 1, 'number')
   checkv(R.test_timet(10000), 10000, 'number')
end

function tests.t01_gireg_19_gtype()
   local R = lgi.Regress
   checkv(R.test_gtype(0), 0, 'number')
   checkv(R.test_gtype(1), 1, 'number')
   checkv(R.test_gtype(10000), 10000, 'number')
end

function tests.t01_gireg_20_closure()
   local R = lgi.Regress
   checkv(R.test_closure(function() return 42 end), 42, 'number')
end

function tests.t01_gireg_21_closure_arg()
   local R = lgi.Regress
   checkv(R.test_closure(function(int) return int end, 43), 43, 'number')
end

function tests.t02_gio_01_loadfile_sync()
   local file = Gio.file_new_for_path('test.lua')
   local ok, contents, length, etag = file:load_contents()
   assert(ok and type(contents) == 'string' and type(length) == 'number' and
    type(etag) == 'string')
end

function tests.t02_gio_02_loadfile_async()
   local file = Gio.file_new_for_path('test.lua')
   local ok, contents, length, etag
   local main = GLib.MainLoop.new()
   file:load_contents_async(nil, function(_, result)
				    ok, contents, length, etag =
				       file:load_contents_finish(result)
				    main:quit()
				 end)
   main:run()
   assert(ok and type(contents) == 'string' and type(length) == 'number' and
    type(etag) == 'string')
end

function tests.t02_gio_03_loadfile_coro()
   local ok, contents, length, etag
   local main = GLib.MainLoop.new()
   coroutine.wrap(
      function()
	 local running = coroutine.running()
	 local file = Gio.file_new_for_path('test.lua')
	 file:load_contents_async(
	    nil, function(f, result)
		    coroutine.resume(running, file:load_contents_finish(result))
		 end)
	 ok, contents, length, etag = coroutine.yield()
	 main:quit()
      end)()
   main:run()
   assert(ok and type(contents) == 'string' and type(length) == 'number' and
    type(etag) == 'string')
end

local tests_passed = 0
local tests_failed = 0

-- Runs specified test from tests table.
local function runtest(name)
   local func = tests[name]
   if type(func) ~= 'function' then
      print(string.format('ERRR: %s is not known test', name))
   else
      local ok, msg = pcall(tests[name])
      if ok then
	 print(string.format('PASS: %s', name))
	 tests_passed = tests_passed + 1
      else
	 print(string.format('FAIL: %s: %s', name, tostring(msg)))
	 tests_failed = tests_failed + 1
      end
   end
end

do
   local names = {}
   for name in pairs(tests) do names[#names + 1] = name end
   table.sort(names)
   for i, name in ipairs(names) do tests[i] = name end
end

-- Run all tests from commandline, or all tests sequentially, if not
-- commandline is given.
local args = {...}
for _, name in ipairs(#args > 0 and args or tests) do runtest(name) end
local tests_total = tests_failed + tests_passed
if tests_failed == 0 then
   print(string.format('All %d tests passed.', tests_total))
else
   print(string.format('%d of %d tests FAILED!', tests_failed, tests_total))
end
