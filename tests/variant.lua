--[[--------------------------------------------------------------------------

  LGI testsuite, GLib.Variant test suite.

  Copyright (c) 2010, 2011 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'
local GLib = lgi.GLib
local GObject = lgi.GObject

local check = testsuite.check

-- Variant testing
local variant = testsuite.group.new('variant')

function variant.gvalue()
   local var1, var2 = GLib.Variant.new_string('foo'),
   GLib.Variant.new_string('bar')
   local val = GObject.Value(GObject.Type.VARIANT, var1)
   check(val.gtype == GObject.Type.VARIANT)
   check(val.value == var1)
   val.value = var2
   check(val.value == var2)
   val.value = nil
   check(val.value == nil)
   check(val.gtype == GObject.Type.VARIANT)
end

function variant.newv_basic()
   local V, v = GLib.Variant
   v = V.new('b', true)
   check(v.type == 'b' and v:get_boolean() == true)
   v = V.new('y', 32)
   check(v.type == 'y' and v:get_byte() == 32)
   v = V.new('n', 13)
   check(v.type == 'n' and v:get_int16() == 13)
   v = V.new('q', 38)
   check(v.type == 'q' and v:get_uint16() == 38)
   v = V.new('i', 32)
   check(v.type == 'i' and v:get_int32() == 32)
   v = V.new('u', 35)
   check(v.type == 'u' and v:get_uint32() == 35)
   v = V.new('x', 39)
   check(v.type == 'x' and v:get_int64() == 39)
   v = V.new('t', 987)
   check(v.type == 't' and v:get_uint64() == 987)
   v = V.new('d', 3.1415927)
   check(v.type == 'd' and v:get_double() == 3.1415927)
   v = V.new('s', 'Hello')
   check(v.type == 's' and v:get_string() == 'Hello')
   v = V.new('o', '/object/path')
   check(v.type == 'o' and v:get_string() == '/object/path')
   v = V.new('g', "asi")
   check(v.type == 'g' and v:get_string() == 'asi')
   local vv = V.new('s', 'inner')
   v = V.new('v', vv)
   check(v.type == 'v' and v:get_variant() == vv)
   v = V.new('ay', 'bytestring')
   check(v.type == 'ay' and tostring(v:get_bytestring()) == 'bytestring')
end

function variant.newv_variant()
   local V, v, vv = GLib.Variant
   vv = V('i', 14)
   v = V('v', vv)
   check(v.type == 'v' and v:n_children() == 1 and v:get_child_value(0) == vv)
end

function variant.newv_maybe()
   local V, v = GLib.Variant
   v = V('mi', 42)
   check(v.type == 'mi' and v:n_children() == 1
	 and v:get_child_value(0).type == 'i'
	 and v:get_child_value(0):get_int32() == 42)
   v = V('mi')
   check(v.type == 'mi' and v:n_children() == 0)
end

function variant.newv_tuple()
   local V, v = GLib.Variant
   v = V.new('()')
   check(v.type == '()' and v:n_children() == 0)
   v = V.new('(i)', {42})
   check(v.type == '(i)' and v:n_children() == 1
	 and v:get_child_value(0).type == 'i'
	 and v:get_child_value(0):get_int32() == 42)
   v = V.new('(mii)', { nil, 1 })
   check(v.type == '(mii)' and v:n_children() == 2
	 and v:get_child_value(0):n_children() == 0)
end

function variant.newv_dictentry()
   local V, v = GLib.Variant
   v = V('{is}', {42, 'Hello'})
   check(v.type == '{is}' and v:n_children() == 2
	 and v:get_child_value(0).type == 'i'
	 and v:get_child_value(0):get_int32() == 42
	 and v:get_child_value(1).type == 's'
	 and v:get_child_value(1):get_string() == 'Hello')
end

function variant.newv_array()
   local V, v = GLib.Variant
   v = V('as', { 'Hello', 'world' })
   check(v.type == 'as' and v:n_children() == 2
	 and v:get_child_value(0):get_string() == 'Hello'
	 and v:get_child_value(1):get_string() == 'world')
   v = V('as', {})
   check(v:n_children() == 0)
   v = V('ams', { 'Hello', nil, 'world', n = 3 })
   check(v:n_children() == 3)
   check(v:get_child_value(0):n_children() == 1
	 and v:get_child_value(0):get_child_value(0):get_string() == 'Hello')
   check(v:get_child_value(1):n_children() == 0)
   check(v:get_child_value(2):n_children() == 1
	 and v:get_child_value(2):get_child_value(0):get_string() == 'world')
end

function variant.newv_dictionary()
   local V, v, vv = GLib.Variant
   v = V('a{sd}', { PI = 3.14, one = 1 })
   check(v:n_children() == 2)
   vv = v:lookup_value('PI', GLib.VariantType.DOUBLE)
   check(vv.type == 'd' and vv:get_double() == 3.14)
   vv = v:lookup_value('one', GLib.VariantType.DOUBLE)
   check(vv.type == 'd' and vv:get_double() == 1)
end

function variant.newv_badtype()
   local V, v = GLib.Variant
   check(not pcall(V.new, '{vs}'))
   check(not pcall(V.new, '{s}'))
   check(not pcall(V.new, '{}'))
   check(not pcall(V.new, '())'))
   check(not pcall(V.new, 'a'))
   check(not pcall(V.new, 'm'))
   check(not pcall(V.new, '{asi}'))
   check(not pcall(V.new, '{mdd}'))
   check(not pcall(V.new, '{is'))
   check(not pcall(V.new, '{is)'))
   check(not pcall(V.new, 'r'))
   check(not pcall(V.new, '*'))
   check(not pcall(V.new, '?'))
   check(not pcall(V.new, 'ii'))
end

function variant.value_simple()
   local V, v = GLib.Variant
   check(V('b', true).value == true)
   check(V('y', 10).value == 10)
   check(V('n', 11).value == 11)
   check(V('q', 12).value == 12)
   check(V('i', 13).value == 13)
   check(V('u', 14).value == 14)
   check(V('q', 15).value == 15)
   check(V('t', 16).value == 16)
   check(V('s', 'I').value == 'I')
   check(V('o', '/o/p').value == '/o/p')
   check(V('g', '(ii)').value == '(ii)')
   v = V('i', 1)
   check(V('v', v).value == v)
   check(V('ay', 'bytestring').value == 'bytestring')
end

function variant.value_container()
   local V, v = GLib.Variant
   check(V('mi', 1).value == 1)
   check(V('mi', nil).value == nil)
   local r
   r = V('{sd}', {'one', 1}).value
   check(type(r) == 'table' and #r == 2 and r[1] == 'one' and r[2] == 1)
   r = V('(imii)', {2, nil, 1}).value
   check(type(r) == 'table' and r.n == 3 and r[1] == 2 and r[2] == nil
	 and r[3] == 1)
   v = V('as', {})
   check(v.value == v)
end

function variant.value_dictionary()
   local V, v = GLib.Variant
   v = V('a{sd}', { one = 1, two = 2 })
   check(v.value.one == 1)
   check(v.value.two == 2)
   check(v.value.three == nil)
   check(v.value[1] == nil)

   v = V('a{is}', { [1] = 'one', [2] = 'two' })
   check(v.value[1] == 'one')
   check(v.value[2] == 'two')
   check(v.value[3] == nil)
   check(v.value.three == nil)
end

function variant.length()
   local V, v = GLib.Variant
   check(#V('s', 'Hello') == 0)
   check(#V('i', 1) == 0)
   check(#V('v', V('i', 1)) == 1)
   check(#V('mi', nil) == 0)
   check(#V('mi', 1) == 1)
   check(#V('(ii)', {1, 2}) == 2)
   check(#V('{sd}', { 'one', 1 }) == 2)
   check(#V('a{sd}', { one = 1 }) == 1)
   check(#V('ai', {}) == 0)
   check(#V('ami', { 1, nil, 2, n = 3 }) == 3)
end

function variant.indexing()
   local V, v = GLib.Variant
   v = V('mi', 1)
   check(v[1] == 1 and v[2] == nil)
   v = V('{sd}', { 'one', 1 })
   check(v[1] == 'one' and v[2] == 1 and v[3] == nil)
   v = V('a{sd}', { one = 1 })
   check(v[1][1] == 'one' and v[1][2] == 1 and v[2] == nil)
   v = V('(si)', { 'hi', 3 })
   check(v[1] == 'hi' and v[2] == 3 and v[3] == nil)
   check(V('s', 'hello')[1] == nil)
end

function variant.serialize()
   local V, v1, v2 = GLib.Variant
   v1 = V('s', 'Hello')
   v2 = V.new_from_data(v1.type, v1.data)
   check(v1:equal(v2))

   -- Make sure that new_from_data properly keeps underlying data alive.
   v1 = nil collectgarbage()
   local _ = v2:print(true)
end
