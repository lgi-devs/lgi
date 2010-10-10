--[[-- Assorted tests. --]]--

require 'lgi'
local GLib = require 'lgi.GLib'
local Gio = require 'lgi.Gio'
local GObject = require 'lgi.GObject'

-- Make logs verbose (do not mute DEBUG level).
lgi.log.DEBUG = 'verbose'

-- Testing infrastructure.
local testgroup = { reverse_index = {} }
testgroup.__index = testgroup

-- Creates new named testgroup.
function testgroup.new(name)
   return setmetatable({ name = name, results = { total = 0, failed = 0 } },
		       testgroup)
end

-- Adds new test.
function testgroup:__newindex(name, func)
   rawset(self, name, func)
   rawset(self, #self + 1, name)
   self.reverse_index[name] = #self
end

-- Runs specified test(s), either by numeric id or by regexp mask.
function testgroup:run(id)
   local function runfunc(num)
      self.results.total = self.results.total + 1
      io.write(('%-8s:%3d:%-30s'):format(self.name, num, self[num]))
      local func = self[self[num]]
      if self.debug then func() else
	 local ok, msg = pcall(func)
	 if not ok then
	    self.results.failed = self.results.failed + 1
	    io.write('FAIL:' .. tostring(msg) .. '\n')
	    return
	 end
      end
      io.write('PASS\n')
   end

   id = id or ''
   self.results.total = 0
   self.results.failed = 0
   if type(id) == 'number' then
      runfunc(id)
   else
      for i = 1, #self do
	 if self[i]:match(id) then runfunc(i) end
      end
      if (self.results.failed == 0) then
	 io.write(('%s: all %d tests passed.\n'):format(
		     self.name, self.results.total))
      else
	 io.write(('%s: %d of %d tests FAILED!\n'):format(
		     self.name, self.results.failed, self.results.total))
      end
   end
end

-- Fails given test with error, number indicates how many functions on
-- the stack should be skipped when reporting error location.
local function fail(msg, skip)
   error(msg or 'failure', (skip or 1) + 1)
end
local function check(cond, msg, skip)
   if not cond then fail(msg, (skip or 1) + 1) end
end

-- Helper, checks that given value has requested type and value.
local function checkv(val, exp, exptype)
   check(type(val) == exptype, string.format("got type `%s', expected `%s'",
					     type(val), exptype), 2)
   check(val == exp, string.format("got value `%s', expected `%s'",
				   tostring(val), tostring(exp)), 2)
end

-- gobject-introspection 'Regress' based tests.
local gireg = testgroup.new('gireg')

function gireg.type_boolean()
   local R = lgi.Regress
   checkv(R.test_boolean(true), true, 'boolean')
   checkv(R.test_boolean(false), false, 'boolean')
   check(select('#', R.test_boolean(true)) == 1)
   check(select('#', R.test_boolean(false)) == 1)
   check(not pcall(R.test_boolean))
   checkv(R.test_boolean(nil), false, 'boolean')
   checkv(R.test_boolean(0), true, 'boolean')
   checkv(R.test_boolean(1), true, 'boolean')
   checkv(R.test_boolean('string'), true, 'boolean')
   checkv(R.test_boolean({}), true, 'boolean')
   checkv(R.test_boolean(function() end), true, 'boolean')
end

function gireg.type_int8()
   local R = lgi.Regress
   checkv(R.test_int8(0), 0, 'number')
   checkv(R.test_int8(1), 1, 'number')
   checkv(R.test_int8(-1), -1, 'number')
   checkv(R.test_int8(1.1), 1, 'number')
   checkv(R.test_int8(-1.1), -1, 'number')
   checkv(R.test_int8(0x7f), 0x7f, 'number')
   checkv(R.test_int8(-0x80), -0x80, 'number')
   check(not pcall(R.test_int8, 0x80))
   check(not pcall(R.test_int8, -0x81))
   check(not pcall(R.test_int8))
   check(not pcall(R.test_int8, nil))
   check(not pcall(R.test_int8, 'string'))
   check(not pcall(R.test_int8, true))
   check(not pcall(R.test_int8, {}))
   check(not pcall(R.test_int8, function() end))
end

function gireg.type_uint8()
   local R = lgi.Regress
   checkv(R.test_uint8(0), 0, 'number')
   checkv(R.test_uint8(1), 1, 'number')
   checkv(R.test_uint8(1.1), 1, 'number')
   checkv(R.test_uint8(0xff), 0xff, 'number')
   check(not pcall(R.test_uint8, 0x100))
   check(not pcall(R.test_uint8, -1))
   check(not pcall(R.test_uint8))
   check(not pcall(R.test_uint8, nil))
   check(not pcall(R.test_uint8, 'string'))
   check(not pcall(R.test_uint8, true))
   check(not pcall(R.test_uint8, {}))
   check(not pcall(R.test_uint8, function() end))
end

function gireg.type_int16()
   local R = lgi.Regress
   checkv(R.test_int16(0), 0, 'number')
   checkv(R.test_int16(1), 1, 'number')
   checkv(R.test_int16(-1), -1, 'number')
   checkv(R.test_int16(1.1), 1, 'number')
   checkv(R.test_int16(-1.1), -1, 'number')
   checkv(R.test_int16(0x7fff), 0x7fff, 'number')
   checkv(R.test_int16(-0x8000), -0x8000, 'number')
   check(not pcall(R.test_int16, 0x8000))
   check(not pcall(R.test_int16, -0x8001))
   check(not pcall(R.test_int16))
   check(not pcall(R.test_int16, nil))
   check(not pcall(R.test_int16, 'string'))
   check(not pcall(R.test_int16, true))
   check(not pcall(R.test_int16, {}))
   check(not pcall(R.test_int16, function() end))
end

function gireg.type_uint16()
   local R = lgi.Regress
   checkv(R.test_uint16(0), 0, 'number')
   checkv(R.test_uint16(1), 1, 'number')
   checkv(R.test_uint16(1.1), 1, 'number')
   checkv(R.test_uint16(0xffff), 0xffff, 'number')
   check(not pcall(R.test_uint16, 0x10000))
   check(not pcall(R.test_uint16, -1))
   check(not pcall(R.test_uint16))
   check(not pcall(R.test_uint16, nil))
   check(not pcall(R.test_uint16, 'string'))
   check(not pcall(R.test_uint16, true))
   check(not pcall(R.test_uint16, {}))
   check(not pcall(R.test_uint16, function() end))
end

function gireg.type_int32()
   local R = lgi.Regress
   checkv(R.test_int32(0), 0, 'number')
   checkv(R.test_int32(1), 1, 'number')
   checkv(R.test_int32(-1), -1, 'number')
   checkv(R.test_int32(1.1), 1, 'number')
   checkv(R.test_int32(-1.1), -1, 'number')
   checkv(R.test_int32(0x7fffffff), 0x7fffffff, 'number')
   checkv(R.test_int32(-0x80000000), -0x80000000, 'number')
   check(not pcall(R.test_int32, 0x80000000))
   check(not pcall(R.test_int32, -0x80000001))
   check(not pcall(R.test_int32))
   check(not pcall(R.test_int32, nil))
   check(not pcall(R.test_int32, 'string'))
   check(not pcall(R.test_int32, true))
   check(not pcall(R.test_int32, {}))
   check(not pcall(R.test_int32, function() end))
end

function gireg.type_uint32()
   local R = lgi.Regress
   checkv(R.test_uint32(0), 0, 'number')
   checkv(R.test_uint32(1), 1, 'number')
   checkv(R.test_uint32(1.1), 1, 'number')
   checkv(R.test_uint32(0xffffffff), 0xffffffff, 'number')
   check(not pcall(R.test_uint32, 0x100000000))
   check(not pcall(R.test_uint32, -1))
   check(not pcall(R.test_uint32))
   check(not pcall(R.test_uint32, nil))
   check(not pcall(R.test_uint32, 'string'))
   check(not pcall(R.test_uint32, true))
   check(not pcall(R.test_uint32, {}))
   check(not pcall(R.test_uint32, function() end))
end

function gireg.type_int64()
   local R = lgi.Regress
   checkv(R.test_int64(0), 0, 'number')
   checkv(R.test_int64(1), 1, 'number')
   checkv(R.test_int64(-1), -1, 'number')
   checkv(R.test_int64(1.1), 1, 'number')
   checkv(R.test_int64(-1.1), -1, 'number')
   check(not pcall(R.test_int64))
   check(not pcall(R.test_int64, nil))
   check(not pcall(R.test_int64, 'string'))
   check(not pcall(R.test_int64, true))
   check(not pcall(R.test_int64, {}))
   check(not pcall(R.test_int64, function() end))

-- Following tests fail because Lua's internal number representation
-- is always 'double', and conversion between double and int64 big
-- constants is always lossy.  Not sure if it can be solved somehow.

--   checkv(R.test_int64(0x7fffffffffffffff), 0x7fffffffffffffff, 'number')
--   checkv(R.test_int64(-0x8000000000000000), -0x8000000000000000, 'number')
--   check(not pcall(R.test_int64, 0x8000000000000000))
--   check(not pcall(R.test_int64, -0x8000000000000001))

end

function gireg.type_uint64()
   local R = lgi.Regress
   checkv(R.test_uint64(0), 0, 'number')
   checkv(R.test_uint64(1), 1, 'number')
   checkv(R.test_uint64(1.1), 1, 'number')
   check(not pcall(R.test_uint64, -1))
   check(not pcall(R.test_uint64))
   check(not pcall(R.test_uint64, nil))
   check(not pcall(R.test_uint64, 'string'))
   check(not pcall(R.test_uint64, true))
   check(not pcall(R.test_uint64, {}))
   check(not pcall(R.test_uint64, function() end))

-- See comment above about lossy conversions.

--   checkv(R.test_uint64(0xffffffffffffffff), 0xffffffffffffffff, 'number')
--   check(not pcall(R.test_uint64, 0x10000000000000000))
end

function gireg.type_short()
   local R = lgi.Regress
   checkv(R.test_short(0), 0, 'number')
   checkv(R.test_short(1), 1, 'number')
   checkv(R.test_short(-1), -1, 'number')
   checkv(R.test_short(1.1), 1, 'number')
   checkv(R.test_short(-1.1), -1, 'number')
end

function gireg.type_ushort()
   local R = lgi.Regress
   checkv(R.test_ushort(0), 0, 'number')
   checkv(R.test_ushort(1), 1, 'number')
   checkv(R.test_ushort(1.1), 1, 'number')
   check(not pcall(R.test_ushort, -1))
end

function gireg.type_int()
   local R = lgi.Regress
   checkv(R.test_int(0), 0, 'number')
   checkv(R.test_int(1), 1, 'number')
   checkv(R.test_int(-1), -1, 'number')
   checkv(R.test_int(1.1), 1, 'number')
   checkv(R.test_int(-1.1), -1, 'number')
end

function gireg.type_uint()
   local R = lgi.Regress
   checkv(R.test_uint(0), 0, 'number')
   checkv(R.test_uint(1), 1, 'number')
   checkv(R.test_uint(1.1), 1, 'number')
   check(not pcall(R.test_uint, -1))
end

function gireg.type_ssize()
   local R = lgi.Regress
   checkv(R.test_ssize(0), 0, 'number')
   checkv(R.test_ssize(1), 1, 'number')
   checkv(R.test_ssize(-1), -1, 'number')
   checkv(R.test_ssize(1.1), 1, 'number')
   checkv(R.test_ssize(-1.1), -1, 'number')
end

function gireg.type_size()
   local R = lgi.Regress
   checkv(R.test_size(0), 0, 'number')
   checkv(R.test_size(1), 1, 'number')
   checkv(R.test_size(1.1), 1, 'number')
   check(not pcall(R.test_size, -1))
end

-- Helper, checks that given value has requested type and value, with some
-- tolerance because of low precision of gfloat type.
local function checkvf(val, exp, tolerance)
   check(type(val) == 'number', string.format(
	     "got type `%s', expected `number'", type(val)), 2)
   check(math.abs(val - exp) <= tolerance,
	  string.format("got value `%s', expected `%s'",
			tostring(val), tostring(exp)), 2)
end

function gireg.type_float()
   local R = lgi.Regress
   local t = 0.0000001
   checkvf(R.test_float(0), 0, t)
   checkvf(R.test_float(1), 1, t)
   checkvf(R.test_float(1.1), 1.1, t)
   checkvf(R.test_float(-1), -1, t)
   checkvf(R.test_float(-1.1), -1.1, t)
   checkvf(R.test_float(0x8000), 0x8000, t)
   checkvf(R.test_float(0xffff), 0xffff, t)
   checkvf(R.test_float(-0x8000), -0x8000, t)
   checkvf(R.test_float(-0xffff), -0xffff, t)
   check(not pcall(R.test_float))
   check(not pcall(R.test_float, nil))
   check(not pcall(R.test_float, 'string'))
   check(not pcall(R.test_float, true))
   check(not pcall(R.test_float, {}))
   check(not pcall(R.test_float, function() end))
end

function gireg.type_double()
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
   check(not pcall(R.test_double))
   check(not pcall(R.test_double, nil))
   check(not pcall(R.test_double, 'string'))
   check(not pcall(R.test_double, true))
   check(not pcall(R.test_double, {}))
   check(not pcall(R.test_double, function() end))
end

function gireg.type_timet()
   local R = lgi.Regress
   checkv(R.test_timet(0), 0, 'number')
   checkv(R.test_timet(1), 1, 'number')
   checkv(R.test_timet(10000), 10000, 'number')
   check(not pcall(R.test_timet))
   check(not pcall(R.test_timet, nil))
   check(not pcall(R.test_timet, 'string'))
   check(not pcall(R.test_timet, true))
   check(not pcall(R.test_timet, {}))
   check(not pcall(R.test_timet, function() end))
end

function gireg.type_gtype()
   local R = lgi.Regress
   checkv(R.test_gtype(0), 0, 'number')
   checkv(R.test_gtype(1), 1, 'number')
   checkv(R.test_gtype(10000), 10000, 'number')
   check(not pcall(R.test_gtype))
   check(not pcall(R.test_gtype, nil))
   check(not pcall(R.test_gtype, 'string'))
   check(not pcall(R.test_gtype, true))
   check(not pcall(R.test_gtype, {}))
   check(not pcall(R.test_gtype, function() end))
end

function gireg.closure()
   local R = lgi.Regress
   checkv(R.test_closure(function() return 42 end), 42, 'number')
end

function gireg.closure_arg()
   local R = lgi.Regress
   checkv(R.test_closure_one_arg(function(int) return int end, 43), 43,
	  'number')
end

function gireg.gvalue_simple()
   local V = GObject.Value
   local function checkv(gval, tp, val)
      check(type(gval.type) == 'string', "GValue.type is not `string'")
      check(gval.type == tp, ("GValue type: expected `%s', got `%s'"):format(
	       tp, gval.type), 2)
      check(gval.value == val, ("GValue value: exp `%s', got `%s'"):format(
	       tostring(val), tostring(gval.value), 2))
   end
   checkv(V(), '', nil)
   checkv(V(0), 'gint', 0)
   checkv(V(1.1), 'gdouble', 1.1)
   checkv(V('str'), 'gchararray', 'str')
   local gcl = GObject.Closure(function() end)
   checkv(V(gcl), 'GClosure', gcl)
   local v = V(42)
   checkv(V(v).value, 'gint', 42)

-- For non-refcounted boxeds, the returned Value.value is always new
-- copy of the instance, so the following test fails:
--
-- checkv(V(v), 'GValue', v)

   check(V(v).type == 'GValue')
end

function gireg.gvalue_arg()
   local R = lgi.Regress
   checkv(R.test_int_value_arg(42), 42, 'number')
end

function gireg.gvalue_return()
   local R = lgi.Regress
   local v = R.test_value_return(43)
   checkv(v.value, 43, 'number')
   check(v.type == 'gint', 'incorrect value type')
end

function gireg.utf8_const_return()
   local R = lgi.Regress
   local utf8_const = 'const \226\153\165 utf8'
   check(R.test_utf8_const_return() == utf8_const)
end

function gireg.utf8_nonconst_return()
   local R = lgi.Regress
   local utf8_nonconst = 'nonconst \226\153\165 utf8'
   check(R.test_utf8_nonconst_return() == utf8_nonconst)
end

function gireg.utf8_const_in()
   local R = lgi.Regress
   local utf8_const = 'const \226\153\165 utf8'
   R.test_utf8_const_in(utf8_const)
end

function gireg.utf8_out()
   local R = lgi.Regress
   local utf8_nonconst = 'nonconst \226\153\165 utf8'
   check(R.test_utf8_out() == utf8_nonconst)
end

function gireg.utf8_inout()
   local R = lgi.Regress
   local utf8_const = 'const \226\153\165 utf8'
   local utf8_nonconst = 'nonconst \226\153\165 utf8'
   check(R.test_utf8_inout(utf8_const) == utf8_nonconst)
end

function gireg.filename_return()
   local R = lgi.Regress
   local fns = R.test_filename_return()
   check(type(fns) == 'table')
   check(#fns == 2)
   check(fns[1] == 'åäö')
   check(fns[2] == '/etc/fstab')
end

function gireg.utf8_int_out_utf8()
   local R = lgi.Regress
   check(R.test_int_out_utf8('') == 0)
   check(R.test_int_out_utf8('abc') == 3)
   local utf8_const = 'const \226\153\165 utf8'
   check(R.test_int_out_utf8(utf8_const) == 12)
end

function gireg.multi_double_args()
   local R = lgi.Regress
   local o1, o2 = R.test_multi_double_args(1)
   check(o1 == 2 and o2 == 3)
   check(#{R.test_multi_double_args(1)} == 2)
end

function gireg.utf8_out_out()
   local R = lgi.Regress
   local o1, o2 = R.test_utf8_out_out()
   check(o1 == 'first' and o2 == 'second')
   check(#{R.test_utf8_out_out()} == 2)
end

function gireg.utf8_out_nonconst_return()
   local R = lgi.Regress
   local o1, o2 = R.test_utf8_out_nonconst_return()
   check(o1 == 'first' and o2 == 'second')
   check(#{R.test_utf8_out_nonconst_return()} == 2)
end

function gireg.utf8_null_in()
   local R = lgi.Regress
   R.test_utf8_null_in(nil)
   R.test_utf8_null_in()
end

function gireg.utf8_null_out()
   local R = lgi.Regress
   check(R.test_utf8_null_out() == nil)
end

function gireg.array_int_in()
   local R = lgi.Regress
   check(R.test_array_int_in{1,2,3} == 6)
   check(R.test_array_int_in{1.1,2,3} == 6)
   check(R.test_array_int_in{} == 0)
   check(not pcall(R.test_array_int_in, nil))
   check(not pcall(R.test_array_int_in, 'help'))
   check(not pcall(R.test_array_int_in, {'help'}))
end

function gireg.array_int_out()
   local R = lgi.Regress
   local a = R.test_array_int_out()
   check(#a == 5)
   check(a[1] == 0 and a[2] == 1 and a[3] == 2 and a[4] == 3 and a[5] == 4)
   check(#{R.test_array_int_out()} == 1)
end

function gireg.array_int_inout()
   local R = lgi.Regress
   local a = R.test_array_int_inout({1, 2, 3, 4, 5})
   check(#a == 4)
   check(a[1] == 3 and a[2] == 4 and a[3] == 5 and a[4] == 6)
   check(#{R.test_array_int_inout({1, 2, 3, 4, 5})} == 1)
   check(not pcall(R.test_array_int_inout, nil))
   check(not pcall(R.test_array_int_inout, 'help'))
   check(not pcall(R.test_array_int_inout, {'help'}))
end

function gireg.array_gint8_in()
   local R = lgi.Regress
   check(R.test_array_gint8_in{1,2,3} == 6)
   check(R.test_array_gint8_in{1.1,2,3} == 6)
   check(R.test_array_gint8_in{} == 0)
   check(not pcall(R.test_array_gint8_in, nil))
   check(not pcall(R.test_array_gint8_in, 'help'))
   check(not pcall(R.test_array_gint8_in, {'help'}))
end

function gireg.array_gint16_in()
   local R = lgi.Regress
   check(R.test_array_gint16_in{1,2,3} == 6)
   check(R.test_array_gint16_in{1.1,2,3} == 6)
   check(R.test_array_gint16_in{} == 0)
   check(not pcall(R.test_array_gint16_in, nil))
   check(not pcall(R.test_array_gint16_in, 'help'))
   check(not pcall(R.test_array_gint16_in, {'help'}))
end

function gireg.array_gint32_in()
   local R = lgi.Regress
   check(R.test_array_gint32_in{1,2,3} == 6)
   check(R.test_array_gint32_in{1.1,2,3} == 6)
   check(R.test_array_gint32_in{} == 0)
   check(not pcall(R.test_array_gint32_in, nil))
   check(not pcall(R.test_array_gint32_in, 'help'))
   check(not pcall(R.test_array_gint32_in, {'help'}))
end

function gireg.array_gint64_in()
   local R = lgi.Regress
   check(R.test_array_gint64_in{1,2,3} == 6)
   check(R.test_array_gint64_in{1.1,2,3} == 6)
   check(R.test_array_gint64_in{} == 0)
   check(not pcall(R.test_array_gint64_in, nil))
   check(not pcall(R.test_array_gint64_in, 'help'))
   check(not pcall(R.test_array_gint64_in, {'help'}))
end

function gireg.array_strv_in()
   local R = lgi.Regress
   check(R.test_strv_in{'1', '2', '3'})
   check(not pcall(R.test_strv_in))
   check(not pcall(R.test_strv_in, '1'))
   check(not pcall(R.test_strv_in, 1))
   check(not R.test_strv_in{'3', '2', '1'})
   check(not R.test_strv_in{'1', '2', '3', '4'})
end

function gireg.array_gtype_in()
   local R = lgi.Regress
   local str = R.test_array_gtype_in {
      lgi.GObject.Value[0].gtype,
      lgi.GObject.type_from_name('gchar')
   }
   check(str == '[GValue,gchar,]')
   check(R.test_array_gtype_in({}) == '[]')
   check(not pcall(R.test_array_gtype_in))
   check(not pcall(R.test_array_gtype_in, ''))
   check(not pcall(R.test_array_gtype_in, 1))
   check(not pcall(R.test_array_gtype_in, function() end))
end

function gireg.array_strv_out()
   local R = lgi.Regress
   local a = R.test_strv_out()
   check(type(a) == 'table' and #a == 5)
   check(table.concat(a, ' ') == 'thanks for all the fish')
   check(#{R.test_strv_out()} == 1)
end

function gireg.array_strv_out_container()
   local R = lgi.Regress
   local a = R.test_strv_out_container()
   check(type(a) == 'table' and #a == 3)
   check(table.concat(a, ' ') == '1 2 3')
end

function gireg.array_strv_outarg()
   local R = lgi.Regress
   local a = R.test_strv_outarg()
   check(type(a) == 'table' and #a == 3)
   check(table.concat(a, ' ') == '1 2 3')
   check(#{R.test_strv_outarg()} == 1)
end

function gireg.array_fixed_size_int_out()
   local R = lgi.Regress
   local a = R.test_array_fixed_size_int_out()
   check(type(a) == 'table' and #a == 5)
   check(a[1] == 0 and a[2] == 1 and a[3] == 2 and a[4] == 3 and a[5] == 4)
   check(#{R.test_array_fixed_size_int_out()} == 1)
end

function gireg.array_fixed_size_int_return()
   local R = lgi.Regress
   local a = R.test_array_fixed_size_int_return()
   check(type(a) == 'table' and #a == 5)
   check(a[1] == 0 and a[2] == 1 and a[3] == 2 and a[4] == 3 and a[5] == 4)
   check(#{R.test_array_fixed_size_int_return()} == 1)
end

function gireg.array_strv_out_c()
   local R = lgi.Regress
   local a = R.test_strv_out_c()
   check(type(a) == 'table' and #a == 5)
   check(table.concat(a, ' ') == 'thanks for all the fish')
end

function gireg.array_int_full_out()
   local R = lgi.Regress
   local a = R.test_array_int_full_out()
   check(type(a) == 'table' and #a == 5)
   check(a[1] == 0 and a[2] == 1 and a[3] == 2 and a[4] == 3 and a[5] == 4)
   check(#{R.test_array_int_full_out()} == 1)
end

function gireg.array_int_full_out()
   local R = lgi.Regress
   local a = R.test_array_int_full_out()
   check(type(a) == 'table' and #a == 5)
   check(a[1] == 0 and a[2] == 1 and a[3] == 2 and a[4] == 3 and a[5] == 4)
   check(#{R.test_array_int_full_out()} == 1)
end

function gireg.array_int_null_in()
   local R = lgi.Regress
   R.test_array_int_null_in()
   R.test_array_int_null_in(nil)
end

function gireg.array_int_null_out()
   local R = lgi.Regress
   local a = R.test_array_int_null_out()
   check(a == nil)
end

function gireg.glist_nothing_return()
   local R = lgi.Regress
   check(select('#', R.test_glist_nothing_return()) == 1)
   a = R.test_glist_nothing_return()
   check(type(a) == 'table' and #a == 3)
   check(a[1] == '1' and a[2] == '2' and a[3] == '3')
end

function gireg.glist_nothing_return2()
   local R = lgi.Regress
   check(select('#', R.test_glist_nothing_return2()) == 1)
   a = R.test_glist_nothing_return2()
   check(type(a) == 'table' and #a == 3)
   check(a[1] == '1' and a[2] == '2' and a[3] == '3')
end

function gireg.glist_container_return()
   local R = lgi.Regress
   check(select('#', R.test_glist_container_return()) == 1)
   a = R.test_glist_container_return()
   check(type(a) == 'table' and #a == 3)
   check(a[1] == '1' and a[2] == '2' and a[3] == '3')
end

function gireg.glist_everything_return()
   local R = lgi.Regress
   check(select('#', R.test_glist_everything_return()) == 1)
   a = R.test_glist_everything_return()
   check(type(a) == 'table' and #a == 3)
   check(a[1] == '1' and a[2] == '2' and a[3] == '3')
end

function gireg.glist_nothing_in()
   local R = lgi.Regress
   R.test_glist_nothing_in  {'1', '2', '3'}
end

function gireg.glist_nothing_in2()
   local R = lgi.Regress
   R.test_glist_nothing_in2  {'1', '2', '3'}
end

function gireg.glist_null_in()
   local R = lgi.Regress
   R.test_glist_null_in {}
   R.test_glist_null_in(nil)
   R.test_glist_null_in()
end

function gireg.glist_null_out()
   local R = lgi.Regress
   check(select('#', R.test_glist_null_out()) == 1)
   local a = R.test_glist_null_out()
   check(type(a) == 'table' and #a == 0)
end

function gireg.gslist_nothing_return()
   local R = lgi.Regress
   check(select('#', R.test_gslist_nothing_return()) == 1)
   a = R.test_gslist_nothing_return()
   check(type(a) == 'table' and #a == 3)
   check(a[1] == '1' and a[2] == '2' and a[3] == '3')
end

function gireg.gslist_nothing_return2()
   local R = lgi.Regress
   check(select('#', R.test_gslist_nothing_return2()) == 1)
   a = R.test_gslist_nothing_return2()
   check(type(a) == 'table' and #a == 3)
   check(a[1] == '1' and a[2] == '2' and a[3] == '3')
end

function gireg.gslist_container_return()
   local R = lgi.Regress
   check(select('#', R.test_gslist_container_return()) == 1)
   a = R.test_gslist_container_return()
   check(type(a) == 'table' and #a == 3)
   check(a[1] == '1' and a[2] == '2' and a[3] == '3')
end

function gireg.gslist_everything_return()
   local R = lgi.Regress
   check(select('#', R.test_gslist_everything_return()) == 1)
   a = R.test_gslist_everything_return()
   check(type(a) == 'table' and #a == 3)
   check(a[1] == '1' and a[2] == '2' and a[3] == '3')
end

function gireg.gslist_nothing_in()
   local R = lgi.Regress
   R.test_gslist_nothing_in  {'1', '2', '3'}
end

function gireg.gslist_nothing_in2()
   local R = lgi.Regress
   R.test_gslist_nothing_in2  {'1', '2', '3'}
end

function gireg.gslist_null_in()
   local R = lgi.Regress
   R.test_gslist_null_in {}
   R.test_gslist_null_in(nil)
   R.test_gslist_null_in()
end

function gireg.gslist_null_out()
   local R = lgi.Regress
   check(select('#', R.test_gslist_null_out()) == 1)
   local a = R.test_gslist_null_out()
   check(type(a) == 'table' and #a == 0)
end

function gireg.ghash_null_return()
   local R = lgi.Regress
   check(select('#', R.test_ghash_null_return()) == 1)
   check(R.test_ghash_null_return() == nil)
end

local function size_htab(h)
   local size = 0
   for _ in pairs(h) do size = size + 1 end
   return size
end

function gireg.ghash_nothing_return()
   local R = lgi.Regress
   local count = 0
   check(select('#', R.test_ghash_nothing_return()) == 1)
   local h = R.test_ghash_nothing_return()
   check(type(h) == 'table')
   check(size_htab(h) == 3)
   check(h.foo == 'bar' and h.baz == 'bat' and h.qux == 'quux')
end

function gireg.ghash_container_return()
   local R = lgi.Regress
   local count = 0
   check(select('#', R.test_ghash_container_return()) == 1)
   local h = R.test_ghash_container_return()
   check(type(h) == 'table')
   check(size_htab(h) == 3)
   check(h.foo == 'bar' and h.baz == 'bat' and h.qux == 'quux')
end

function gireg.ghash_everything_return()
   local R = lgi.Regress
   local count = 0
   check(select('#', R.test_ghash_everything_return()) == 1)
   local h = R.test_ghash_everything_return()
   check(type(h) == 'table')
   check(size_htab(h) == 3)
   check(h.foo == 'bar' and h.baz == 'bat' and h.qux == 'quux')
end

function gireg.ghash_null_in()
   local R = lgi.Regress
   R.test_ghash_null_in(nil)
   R.test_ghash_null_in()
   check(not pcall(R.test_ghash_null_in,1))
   check(not pcall(R.test_ghash_null_in,'string'))
   check(not pcall(R.test_ghash_null_in,function() end))
end

function gireg.ghash_null_out()
   local R = lgi.Regress
   check(R.test_ghash_null_out() == nil)
end

function gireg.ghash_nothing_in()
   local R = lgi.Regress
   R.test_ghash_nothing_in({ foo = 'bar', baz = 'bat', qux = 'quux' })
   check(not pcall(R.test_ghash_nothing_in))
   check(not pcall(R.test_ghash_nothing_in, 1))
   check(not pcall(R.test_ghash_nothing_in, 'test'))
   check(not pcall(R.test_ghash_nothing_in, function() end))
end

function gireg.ghash_nested_everything_return()
   local R = lgi.Regress
   check(select('#', R.test_ghash_nested_everything_return) == 1);
   local a = R.test_ghash_nested_everything_return()
   check(type(a) == 'table')
   check(size_htab(a) == 1)
   check(type(a.wibble) == 'table')
   check(size_htab(a.wibble) == 3)
   check(a.wibble.foo == 'bar' and a.wibble.baz == 'bat'
	 and a.wibble.qux == 'quux')
end

function gireg.enum()
   local R = lgi.Regress
   check(R.TestEnum.VALUE1 == 0)
   check(R.TestEnum.VALUE2 == 1)
   check(R.TestEnum.VALUE3 == 42)
   check(R.TestEnum[0] == 'VALUE1')
   check(R.TestEnum[1] == 'VALUE2')
   check(R.TestEnum[42] == 'VALUE3')
   check(R.TestEnum[43] == nil)
   check(R.test_enum_param(0) == 'value1')
   check(R.test_enum_param(1) == 'value2')
   check(R.test_enum_param(42) == 'value3')
end

function gireg.flags()
   local R = lgi.Regress
   check(R.TestFlags.FLAG1 == 1)
   check(R.TestFlags.FLAG2 == 2)
   check(R.TestFlags.FLAG3 == 4)
   check(R.TestFlags[7].FLAG1 == 1)
   check(R.TestFlags[7].FLAG2 == 2)
   check(R.TestFlags[7].FLAG3 == 4)
   check(R.TestFlags[3].FLAG1 == 1)
   check(R.TestFlags[3].FLAG2 == 2)
   check(R.TestFlags[3].FLAG3 == nil)
end

function gireg.struct_a()
   local R = lgi.Regress
   check(select('#', R.TestStructA()) == 1)
   local a = R.TestStructA()
   check(type(a) == 'userdata')
   a.some_int = 42
   check(a.some_int == 42)
   a.some_int8 = 12
   check(a.some_int8 == 12)
   a.some_double = 3.14
   check(a.some_double == 3.14)
   a.some_enum = R.TestEnum.VALUE2
   check(a.some_enum == R.TestEnum.VALUE2)
   a = R.TestStructA { some_int = 42, some_int8 = 12,
		       some_double = 3.14, some_enum = R.TestEnum.VALUE2 }
   a.some_int = 43
   a.some_int8 = 13
   check(a.some_int == 43)
   check(a.some_int8 == 13)
   check(a.some_double == 3.14)
   check(a.some_enum == R.TestEnum.VALUE2)
   a.some_double = 3.15
   check(a.some_int == 43)
   check(a.some_int8 == 13)
   check(a.some_double == 3.15)
   check(a.some_enum == R.TestEnum.VALUE2)
   a.some_enum = R.TestEnum.VALUE3
   check(a.some_int == 43)
   check(a.some_int8 == 13)
   check(a.some_double == 3.15)
   check(a.some_enum == R.TestEnum.VALUE3)
   check(not pcall(function() return a.foo end))
   check(not pcall(function() a.foo = 1 end))
   check(select('#', (function() a.some_int = 0 end)()) == 0)
   check(select('#', (function() return a.some_int end)()) == 1)
   check(select('#', (function() local b = a.some_int end)()) == 0)
end

function gireg.struct_a_clone()
   local R = lgi.Regress
   local a = R.TestStructA { some_int = 42, some_int8 = 12, some_double = 3.14,
			     some_enum = R.TestEnum.VALUE2 }
   check(select('#', a:clone()) == 1)
   local b = a:clone()
   check(type(b) == 'userdata')
   check(b ~= a)
   check(b.some_int == 42)
   check(b.some_int8 == 12)
   check(b.some_double == 3.14)
   check(b.some_enum == R.TestEnum.VALUE2)
   check(a.some_int == 42)
   check(a.some_int8 == 12)
   check(a.some_double == 3.14)
   check(a.some_enum == R.TestEnum.VALUE2)
end

function gireg.struct_b()
   local R = lgi.Regress
   local b = R.TestStructB()

   -- Basic fields assignments.
   b.some_int8 = 13
   check(b.some_int8 == 13)
   b.nested_a.some_int = -1
   check(b.some_int8 == 13)
   check(b.nested_a.some_int == -1)
   b.nested_a.some_int8 = -2
   check(b.some_int8 == 13)
   check(b.nested_a.some_int == -1)
   check(b.nested_a.some_int8 == -2)

   -- Whole nested structure assignment.
   b.nested_a = { some_int = 42, some_int8 = 12,
		  some_double = 3.14, some_enum = R.TestEnum.VALUE2 }
   check(b.nested_a.some_int == 42)
   check(b.nested_a.some_int8 == 12)
   check(b.nested_a.some_double == 3.14)
   check(b.nested_a.some_enum == R.TestEnum.VALUE2)

   -- Nested structure construction.
   b = R.TestStructB { some_int8 = 21, nested_a =
		       { some_int = 42, some_int8 = 12,
			 some_double = 3.14, some_enum = R.TestEnum.VALUE2 } }
   check(b.some_int8 == 21)
   check(b.nested_a.some_int == 42)
   check(b.nested_a.some_int8 == 12)
   check(b.nested_a.some_double == 3.14)
   check(b.nested_a.some_enum == R.TestEnum.VALUE2)
end

-- Available groups
local groups = { 'gireg', gireg = gireg }

-- Cmdline runner.
local failed = false
args = args or {}
if #args == 0 then
   -- Check for debug mode.
   if tests_debug then
      for _, name in ipairs(groups) do
	 groups[name].debug = true
	 _G[name] = groups[name]
      end
      return
   end

   -- Run everything.
   for _, group in ipairs(groups) do
      groups[group]:run()
      failed = failed or groups[group].results.failed > 0
   end
else
   -- Run just those which pass the mask.
   for _, mask in ipairs(args) do
      local groupname, groupmask = mask:match('^(.-):(.+)$')
      if not groupname or not groups[group] then
	 io.write(("No test group for mask `%s' found."):format(mask))
	 return 2
      end
      groups[group]:run(groupmask)
      failed = failed or groups[group].results.failed > 0
   end
end
return not failed and 0 or 1
