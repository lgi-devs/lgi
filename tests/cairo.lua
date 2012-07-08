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
	 checkv(t, 'MOVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 3)
	 checkv(pts[1].x, 1, 'number')
	 checkv(pts[1].y, 2, 'number')
	 checkv(pts[2].x, 3, 'number')
	 checkv(pts[2].y, 4, 'number')
	 checkv(pts[3].x, 5, 'number')
	 checkv(pts[3].y, 6, 'number')
      elseif i == 3 then
	 checkv(t, 'MOVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 0)
      elseif i == 4 then
	 checkv(t, 'LINE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 1)
	 checkv(pts[1].x, 21, 'number')
	 checkv(pts[1].y, 22, 'number')
      else
	 check(false)
      end
      i = i + 1
   end
   check(i == 4)
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
