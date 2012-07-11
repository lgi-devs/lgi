--[[--------------------------------------------------------------------------

  LGI testsuite, cairo overrides test group.

  Copyright (c) 2012 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local io = require 'io'
local os = require 'os'
local lgi = require 'lgi'

local check = testsuite.check
local checkv = testsuite.checkv
local cairo = testsuite.group.new('cairo')

function cairo.matrix()
   local cairo = lgi.cairo

   local matrix = cairo.Matrix()
   checkv(matrix.xx, 0, 'number')
   checkv(matrix.yx, 0, 'number')
   checkv(matrix.xy, 0, 'number')
   checkv(matrix.yy, 0, 'number')
   checkv(matrix.x0, 0, 'number')
   checkv(matrix.y0, 0, 'number')

   matrix = cairo.Matrix { xx = 1, yx =1.5,
			   xy = 2, yy = 2.5,
			   x0 = 3, y0 = 3.5 }
   checkv(matrix.xx, 1, 'number')
   checkv(matrix.yx, 1.5, 'number')
   checkv(matrix.xy, 2, 'number')
   checkv(matrix.yy, 2.5, 'number')
   checkv(matrix.x0, 3, 'number')
   checkv(matrix.y0, 3.5, 'number')
end

function cairo.matrix_getset()
   local cairo = lgi.cairo
   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   local cr = cairo.Context(surface)

   local m = cairo.Matrix { xx = 1, yx =1.5,
			    xy = 2, yy = 2.5,
			    x0 = 3, y0 = 3.5 }
   cr.matrix = m
   local m2 = cr.matrix
   check(m.xx == m2.xx)
   check(m.yx == m2.yx)
   check(m.xy == m2.xy)
   check(m.yy == m2.yy)
   check(m.x0 == m2.x0)
   check(m.y0 == m2.y0)
end

function cairo.dash()
   local cairo = lgi.cairo
   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   local cr = cairo.Context(surface)

   local dash, offset = cr:get_dash()
   check(type(dash) == 'table')
   check(next(dash) == nil)

   cr:set_dash({ 1, 2, math.pi }, 2.22)
   dash, offset = cr:get_dash()
   check(#dash == 3)
   check(dash[1] == 1)
   check(dash[2] == 2)
   check(dash[3] == math.pi)
   check(offset == 2.22)

   cr:set_dash(nil, 0)
   dash, offset = cr:get_dash()
   check(type(dash) == 'table')
   check(next(dash) == nil)
end

function cairo.path()
   local cairo = lgi.cairo
   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   local cr = cairo.Context(surface)

   cr:move_to(10, 11)
   cr:curve_to(1, 2, 3, 4, 5, 6)
   cr:close_path()
   cr:line_to(21, 22)

   local i = 1
   for t, pts in cr:copy_path():pairs() do
      if i == 1 then
	 checkv(t, 'MOVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 1)
	 checkv(pts[1].x, 10, 'number')
	 checkv(pts[1].y, 11, 'number')
      elseif i == 2 then
	 checkv(t, 'CURVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 3)
	 checkv(pts[1].x, 1, 'number')
	 checkv(pts[1].y, 2, 'number')
	 checkv(pts[2].x, 3, 'number')
	 checkv(pts[2].y, 4, 'number')
	 checkv(pts[3].x, 5, 'number')
	 checkv(pts[3].y, 6, 'number')
      elseif i == 3 then
	 checkv(t, 'CLOSE_PATH', 'string')
	 check(type(pts) == 'table' and #pts == 0)
      elseif i == 4 then
	 checkv(t, 'MOVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 1)
	 checkv(pts[1].x, 10, 'number')
	 checkv(pts[1].y, 11, 'number')
      elseif i == 5 then
	 checkv(t, 'LINE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 1)
	 checkv(pts[1].x, 21, 'number')
	 checkv(pts[1].y, 22, 'number')
      else
	 check(false)
      end
      i = i + 1
   end
   check(i == 6)
end

function cairo.surface_type()
   local cairo = lgi.cairo
   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   local cr = cairo.Context(surface)

   check(cairo.ImageSurface:is_type_of(surface))
   check(cairo.Surface:is_type_of(surface))
   check(not cairo.RecordingSurface:is_type_of(surface))

   local s2 = cr.target
   check(cairo.ImageSurface:is_type_of(s2))
   check(cairo.Surface:is_type_of(s2))
   check(not cairo.RecordingSurface:is_type_of(s2))
end

function cairo.context_transform()
   local cairo = lgi.cairo
   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   local cr = cairo.Context(surface)

   function compare(a, b)
      check(math.abs(a-b) < 0.1)
   end

   cr:rotate(-math.pi / 2)
   cr:translate(100, 200)

   local x, y = cr:user_to_device(10, 20)
   compare(x, 220)
   compare(y, -110)

   local x, y = cr:device_to_user(220, -110)
   compare(x, 10)
   compare(y, 20)

   local x, y = cr:user_to_device_distance(10, 20)
   compare(x, 20)
   compare(y, -10)

   local x, y = cr:device_to_user_distance(20, -10)
   compare(x, 10)
   compare(y, 20)
end

function cairo.mesh()
   local cairo = lgi.cairo

   -- Mesh patterns are introduced in cairo 1.12
   if cairo.version < cairo.version_encode(1, 12, 0) then return end

   local mesh = cairo.Pattern.create_mesh()
   local pattern = cairo.Pattern.create_radial(1, 2, 3, 4, 5, 6)

   check(cairo.Pattern:is_type_of(mesh))
   check(cairo.MeshPattern:is_type_of(mesh))

   check(cairo.Pattern:is_type_of(pattern))
   check(not cairo.MeshPattern:is_type_of(pattern))

   local function check_status(status)
       checkv(status, 'SUCCESS', 'string')
   end

   -- Taken from cairo's pattern-getters test and slightly adapted to use all
   -- functions of the mesh pattern API
   local status, count = mesh:get_patch_count()
   check_status(status)
   checkv(count, 0, 'number')

   mesh:begin_patch()
   mesh:move_to(0, 0)
   mesh:line_to(0, 3)
   mesh:line_to(3, 3)
   mesh:line_to(3, 0)
   mesh:set_corner_color_rgba(0, 1, 1, 1, 1)
   mesh:end_patch()

   local status, count = mesh:get_patch_count()
   check_status(status)
   checkv(count, 1, 'number')

   for k, v in pairs({ { 1, 1 }, { 1, 2 }, { 2, 2 }, { 2, 1 } }) do
       local status, x, y = mesh:get_control_point(0, k - 1)
       check_status(status)
       checkv(x, v[1], 'number')
       checkv(y, v[2], 'number')
   end

   mesh:begin_patch()
   mesh:move_to(0, 0)
   mesh:line_to(1, 0)
   mesh:curve_to(1, 1, 1, 2, 0, 1)
   mesh:set_corner_color_rgb(0, 1, 1, 1)
   mesh:set_control_point(2, 0.5, 0.5)
   mesh:end_patch()

   local status, count = mesh:get_patch_count()
   check_status(status)
   checkv(count, 2, 'number')

   for k, v in pairs({ 1, 0, 0, 1 }) do
       local status, r, g, b, a = mesh:get_corner_color_rgba(1, k - 1)
       check_status(status)
       checkv(r, v, 'number')
       checkv(g, v, 'number')
       checkv(b, v, 'number')
       checkv(a, v, 'number')
   end

   local i = 0
   local expected = {
      { { 0, 1 }, { 0, 2 }, { 0, 3 } },
      { { 1, 3 }, { 2, 3 }, { 3, 3 } },
      { { 3, 2 }, { 3, 1 }, { 3, 0 } },
      { { 2, 0 }, { 1, 0 }, { 0, 0 } },
   }
   for t, pts in mesh:get_path(0):pairs() do
      if i == 0 then
	 checkv(t, 'MOVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 1)
	 checkv(pts[1].x, 0, 'number')
	 checkv(pts[1].y, 0, 'number')
      else
	 -- Mesh patterns turn everything into curves. :-(
	 checkv(t, 'CURVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 3)
	 for k, v in pairs(expected[i]) do
	    checkv(pts[k].x, v[1], 'number')
	    checkv(pts[k].y, v[2], 'number')
	 end
      end
      i = i + 1
   end
   check(i == #expected + 1)
end
